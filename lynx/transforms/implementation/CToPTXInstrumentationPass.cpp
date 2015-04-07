/*! \file CToPTXInstrumentationPass.cpp
	\date Tuesday July 1, 2011
	\author Naila Farooqui <naila@cc.gatech.edu>
	\brief The source file for the CToPTXInstrumentationPass class
*/

#ifndef C_TO_PTX_INSTRUMENTATION_PASS_CPP_INCLUDED
#define C_TO_PTX_INSTRUMENTATION_PASS_CPP_INCLUDED

#include <lynx/transforms/interface/CToPTXInstrumentationPass.h>
#include <ocelot/ir/interface/Module.h>
#include <ocelot/ir/interface/PTXStatement.h>
#include <ocelot/ir/interface/PTXInstruction.h>
#include <ocelot/ir/interface/PTXOperand.h>
#include <ocelot/ir/interface/PTXKernel.h>
#include <ocelot/analysis/interface/DataflowGraph.h>

#include <hydrazine/interface/Exception.h>
#define REPORT_BASE 1 
namespace transforms
{
    analysis::DataflowGraph& CToPTXInstrumentationPass::dfg()
	{
		analysis::Analysis* graph = getAnalysis(
			"DataflowGraphAnalysis");

		assert(graph != 0);
		
		return static_cast<analysis::DataflowGraph&>(*graph);
	}


    /* This method assigns unused registers for each PTX statement to be inserted and replaces all static attribute place-holders with actual values */
    ir::PTXStatement CToPTXInstrumentationPass::prepareStatementToInsert(ir::PTXStatement statement, StaticAttributes attributes) {
    
        ir::PTXStatement toInsert = statement;
        
        if(statement.instruction.opcode == ir::PTXInstruction::Call)
        { 
            toInsert.instruction.pg.reg = newRegisterMap[toInsert.instruction.pg.identifier];
            toInsert.instruction.pg.identifier.clear();
            return toInsert;
        }
        
        ir::PTXOperand ir::PTXInstruction::* source_operands[] = { &ir::PTXInstruction::a, & ir::PTXInstruction::b, & ir::PTXInstruction::c};

        for (int i = 0; i < 3; i++) 
        {
            ir::PTXOperand &operand = statement.instruction.*(source_operands[i]);
        
            if(operand.identifier == BASIC_BLOCK_COUNT || 
                operand.identifier == BASIC_BLOCK_INST_COUNT ||
                operand.identifier == BASIC_BLOCK_EXEC_INST_COUNT ||
                operand.identifier == BASIC_BLOCK_ID ||
                operand.identifier == INSTRUCTION_COUNT ||
                operand.identifier == INSTRUCTION_ID)
                    (toInsert.instruction.*(source_operands[i])).addressMode = ir::PTXOperand::Immediate;
        
            if( operand.identifier == BASIC_BLOCK_COUNT)
                (toInsert.instruction.*(source_operands[i])).imm_uint = attributes.basicBlockCount;
            else if( operand.identifier == BASIC_BLOCK_INST_COUNT) 
                (toInsert.instruction.*(source_operands[i])).imm_uint = attributes.basicBlockInstructionCount;
            else if( operand.identifier == BASIC_BLOCK_EXEC_INST_COUNT || operand.identifier == BASIC_BLOCK_PRED_INST_COUNT)
                (toInsert.instruction.*(source_operands[i])).imm_uint = attributes.basicBlockExecutedInstructionCount;
            else if( operand.identifier == BASIC_BLOCK_ID)
                (toInsert.instruction.*(source_operands[i])).imm_uint = attributes.basicBlockId;
            else if( operand.identifier == INSTRUCTION_ID)
                (toInsert.instruction.*(source_operands[i])).imm_uint = attributes.instructionId;
            else if( operand.identifier == INSTRUCTION_COUNT)
                (toInsert.instruction.*(source_operands[i])).imm_uint = attributes.kernelInstructionCount;
       } 
       
        if(statement.instruction.d.identifier == COMPUTE_BASE_ADDRESS)
        {
            return computeBaseAddress(statement, attributes.originalInstruction);
        }
        
        if(statement.instruction.opcode == ir::PTXInstruction::Bra)
        {
            if(statement.instruction.pg.condition == ir::PTXOperand::Pred || statement.instruction.pg.condition == ir::PTXOperand::InvPred)
            {
                toInsert.instruction.pg.reg = newRegisterMap[statement.instruction.pg.identifier];
                toInsert.instruction.pg.identifier.clear();
            }
            if(statement.instruction.d.identifier == EXIT)
            {
                analysis::DataflowGraph::iterator endBlock = --dfg().end();
                --endBlock;
                toInsert.instruction.d.identifier = endBlock->label();
            }

            return toInsert;
        }

        if(statement.instruction.d.identifier == GET_PREDICATE_VALUE)
        {
            toInsert.instruction.opcode = ir::PTXInstruction::SelP;
            toInsert.instruction.c.type = ir::PTXOperand::pred;
            toInsert.instruction.d.addressMode = toInsert.instruction.c.addressMode = ir::PTXOperand::Register;
            toInsert.instruction.a.addressMode = toInsert.instruction.b.addressMode = ir::PTXOperand::Immediate;
            toInsert.instruction.a.imm_uint = 1;
            toInsert.instruction.b.imm_uint = 0;
        
            toInsert.instruction.d.reg = dfg().newRegister();
            newRegisterMap[toInsert.instruction.d.identifier] = toInsert.instruction.d.reg;
            toInsert.instruction.d.identifier.clear();

            toInsert.instruction.c = attributes.originalInstruction.pg;
        }
        
        ir::PTXOperand ir::PTXInstruction::* operands[] = { &ir::PTXInstruction::a, & ir::PTXInstruction::b, & ir::PTXInstruction::c, 
            & ir::PTXInstruction::d, & ir::PTXInstruction::pg };

        for (int i = 0; i < 5; i++) {
            ir::PTXOperand &operand = statement.instruction.*(operands[i]);
           
            if((operand.addressMode == ir::PTXOperand::Register ||
                operand.addressMode == ir::PTXOperand::Indirect ||
                (operand.condition == ir::PTXOperand::Pred && operand.addressMode != ir::PTXOperand::Address)) &&
                 !operand.identifier.empty()) {
                (toInsert.instruction.*(operands[i])).reg = newRegisterMap[operand.identifier];
                (toInsert.instruction.*(operands[i])).identifier.clear();
            }
        }
        
        if(statement.instruction.opcode == ir::PTXInstruction::Vote && statement.instruction.vote == ir::PTXInstruction::Uni){
            if(attributes.originalInstruction.pg.condition == ir::PTXOperand::PT ||
                attributes.originalInstruction.pg.condition == ir::PTXOperand::nPT)
            {
                toInsert.instruction.vote = ir::PTXInstruction::VoteMode_Invalid;

                toInsert.instruction.opcode = ir::PTXInstruction::SetP;
                toInsert.instruction.comparisonOperator = ir::PTXInstruction::Eq;
                toInsert.instruction.type = toInsert.instruction.a.type = toInsert.instruction.b.type = ir::PTXOperand::u64;
                toInsert.instruction.a.addressMode = toInsert.instruction.b.addressMode = ir::PTXOperand::Immediate;
                toInsert.instruction.a.imm_uint = 0;
                toInsert.instruction.b.imm_uint = 0;
            }
            else 
            {
                toInsert.instruction.a = attributes.originalInstruction.pg;
            }
        }
        
        return toInsert;
    }

    bool CToPTXInstrumentationPass::instrumentationConditionsMet(ir::PTXInstruction instruction, TranslationBlock translationBlock)
    {
        bool instructionClassValid = false;
        bool addressSpaceValid = false;
        bool dataTypeValid = false;
                
        if(translationBlock.specifier.instructionClassVector.empty())
        {
            instructionClassValid = true;
        }
        else
        {  
            instructionClassValid = false;

            for (InstrumentationSpecifier::StringVector::iterator instClass = translationBlock.specifier.instructionClassVector.begin(); 
                instClass != translationBlock.specifier.instructionClassVector.end(); ++instClass) 
            {
                if(*instClass == ON_PREDICATED)
                {
                    translationBlock.specifier.isPredicated = true;
                    
                    if(instruction.pg.condition == ir::PTXOperand::Pred || instruction.pg.condition == ir::PTXOperand::InvPred) 
                    {
                       instructionClassValid = true;
                       break;
                    }        
                }
                else if(opcodeMap[instruction.opcode] == *instClass)
                {
                    if(*instClass == ON_BRANCH && translationBlock.specifier.isPredicated)
                        break;
                        
                    instructionClassValid = true;
                    break;
                }       
            }    
        }          
        
        if(translationBlock.specifier.addressSpaceVector.empty())
        {
            addressSpaceValid = true;
        }
        else 
        {
            addressSpaceValid = false;
            for(InstrumentationSpecifier::StringVector::iterator addressSpace = translationBlock.specifier.addressSpaceVector.begin();
                            addressSpace != translationBlock.specifier.addressSpaceVector.end(); ++addressSpace)
            {
                if(addressSpaceMap[*addressSpace] == instruction.addressSpace)
                {
                    addressSpaceValid = true;
                    break;
                }        
            }
        
        }
        
        if(translationBlock.specifier.dataTypeVector.empty())
        {
            dataTypeValid = true;
        }
        else 
        {
            dataTypeValid = false;
            for(InstrumentationSpecifier::StringVector::iterator dataType = translationBlock.specifier.dataTypeVector.begin();
                            dataType != translationBlock.specifier.dataTypeVector.end(); ++dataType)
            {
                if(dataTypeMap[instruction.type] == *dataType)
                {
                    dataTypeValid = true;
                }        
            }
        
        }
        return (instructionClassValid && addressSpaceValid && dataTypeValid);
    }

    void CToPTXInstrumentationPass::insertAt()
    {
	    std::string destToUse, srcToUse;
	    unsigned iIndex = 0, bIndex=0, iIndexToInsert = 0, bIndexToInsert = 0;
        analysis::DataflowGraph::iterator block = dfg().begin();
        for( analysis::DataflowGraph::iterator basicBlock = block; 
            basicBlock != dfg().end(); ++basicBlock )
        {
		bIndex++;
		iIndex = 0;
           if(basicBlock->instructions().empty())
              continue;
            for( analysis::DataflowGraph::InstructionVector::const_iterator instruction = basicBlock->instructions().begin();
                instruction != basicBlock->instructions().end(); ++instruction)
            {
		    iIndex++;
                ir::PTXInstruction *ptxInstruction = (ir::PTXInstruction *)instruction->i;
		std::string identifier("%ctaid.x");
		if(ptxInstruction->a.identifier.compare(identifier) == 0){
			std::cout << "FOUND CTAID (in block " << bIndex - 1 << " and instruction " << iIndex -1 << ") : " << ptxInstruction->a.identifier << " " << ptxInstruction->a.toString() 
				<< " " << ptxInstruction->d.toString() << std::endl;
			//dfg().erase(basicBlock, iIndex-1);
			bIndexToInsert = bIndex - 1;
			//iIndexToInsert = iIndex - 1;
			iIndexToInsert = iIndex;
			//regToUse = ptxInstruction->a;
			destToUse = ptxInstruction->d.toString();
			srcToUse = ptxInstruction->a.toString();
			break;
		}
	        //ir::PTXStatement toInsertAt = ;
            }
        }    
        block = dfg().begin();
	bIndex = 0;
	//Create the instruction : add.u32 %r.X, %ctaid, #imm
        ir::PTXOperand::DataType type = ir::PTXOperand::u32;
        ir::PTXInstruction inst;
        inst.type = type;

	inst.opcode = ir::PTXInstruction::Add;

	inst.d.addressMode = ir::PTXOperand::Register;
	inst.d.type = type;
	inst.d.identifier = destToUse;

	inst.a.addressMode = ir::PTXOperand::Register;
	inst.a.type = type;
	inst.a.identifier = destToUse;

	inst.b.addressMode = ir::PTXOperand::Immediate;
	inst.b.type = type;
	inst.b.imm_uint = 64;

        for( analysis::DataflowGraph::iterator basicBlock = block; 
            basicBlock != dfg().end(); ++basicBlock )
        {
		bIndex++;
		iIndex = 0;
		std::cout << "New block " << bIndex-1 << std::endl;
		if(bIndex - 1 != bIndexToInsert)
			continue;
		std::cout << "Block to be inserted in " << bIndex-1 << std::endl;
           if(basicBlock->instructions().empty())
              continue;
	   std::cout << "Block is not empty" << std::endl;

            for( analysis::DataflowGraph::InstructionVector::const_iterator instruction = basicBlock->instructions().begin();
                instruction != basicBlock->instructions().end(); ++instruction)
            {
		    iIndex++;
		    std::cout << "Block " << bIndex-1 << " instruction " << iIndex-1 << std::endl;
		if(iIndex - 1 != iIndexToInsert)
			continue;
		std::cout << "Instruction to be inserted before " << iIndex-1 << std::endl;
                ir::PTXInstruction *ptxInstruction = (ir::PTXInstruction *)instruction->i;
		std::cout << "Inserting in Block " << bIndex - 1 << " and before instruction " << iIndex - 1 << " : " << ptxInstruction->toString() << std::endl;
		dfg().insert(basicBlock, inst, iIndex-1);
		
		break;
            }
        }    
    }

    unsigned long CToPTXInstrumentationPass::insertBefore(TranslationBlock translationBlock, StaticAttributes attributes, analysis::DataflowGraph::iterator basicBlock, unsigned int loc)
    {
    
        unsigned long count = 0;
        for( unsigned int j = 0; j < translationBlock.statements.size(); j++) {
            ir::PTXStatement toInsert = prepareStatementToInsert(translationBlock.statements.at(j), attributes);
            
            if(toInsert.instruction.opcode == ir::PTXInstruction::Nop)
            {
                continue;
            }
	    std::cout << "insertBefore inserting " << toInsert.instruction.toString() << std::endl;
            dfg().insert(basicBlock, toInsert.instruction, loc++);
	        count++;
        }
        
        return count;
    }
    
    void CToPTXInstrumentationPass::insertAfter(TranslationBlock translationBlock, StaticAttributes attributes, analysis::DataflowGraph::iterator basicBlock, unsigned int loc)
    {
        for( unsigned int j = 0; j < translationBlock.statements.size(); j++) {
            ir::PTXStatement toInsert = prepareStatementToInsert(translationBlock.statements.at(j), attributes);
            if(toInsert.instruction.opcode == ir::PTXInstruction::Nop)
                continue;
            dfg().insert(basicBlock, toInsert.instruction, ++loc);
        }
    }

    void CToPTXInstrumentationPass::instrumentInstruction(TranslationBlock translationBlock) 
    {
	    report("CToPTXInstrumentationPass::instrumentInstruction...");
	insertAt();

        StaticAttributes attributes;
        attributes.basicBlockId = 0;
        attributes.kernelInstructionCount = kernelInstructionCount(translationBlock);
        attributes.basicBlockCount = dfg().size() - 2;
        
        
        analysis::DataflowGraph::iterator block = dfg().begin();
        ++block;
        
        /* Iterating through each basic block */
        for( analysis::DataflowGraph::iterator basicBlock = block; 
            basicBlock != dfg().end(); ++basicBlock )
        {
           if(basicBlock->instructions().empty())
              continue;
            
            /* Update basicBlockInstructionCount to include all instructions in the basic block by default */
            attributes.basicBlockInstructionCount = basicBlock->instructions().size();
            unsigned int loc = 0;
                        
            attributes.instructionId = 0;
            /* Iterating through each instruction */
            for( analysis::DataflowGraph::InstructionVector::const_iterator instruction = basicBlock->instructions().begin();
                instruction != basicBlock->instructions().end(); ++instruction)
            {
            
                ir::PTXInstruction *ptxInstruction = (ir::PTXInstruction *)instruction->i;
                /* Save the instruction for inspection */
                attributes.originalInstruction = *ptxInstruction;
                
                if(instrumentationConditionsMet(*ptxInstruction, translationBlock))
                {
                    loc += insertBefore(translationBlock, attributes, basicBlock, loc);
                    attributes.instructionId++;
                }
                loc++;
            }
            
           attributes.basicBlockId++;          
        }    
    }


    void CToPTXInstrumentationPass::instrumentBasicBlock(TranslationBlock translationBlock) 
    {
        StaticAttributes attributes;
        attributes.basicBlockId = 0;
        attributes.basicBlockInstructionCount = 0;
        attributes.basicBlockCount = dfg().size() - 2;
        
        analysis::DataflowGraph::iterator block = dfg().begin();
        ++block;
        
        /* Iterating through each basic block */
        for( analysis::DataflowGraph::iterator basicBlock = block; 
            basicBlock != dfg().end(); ++basicBlock )
        {        
           if(basicBlock->instructions().empty())
              continue;
                
            attributes.basicBlockInstructionCount = 0;
            attributes.basicBlockExecutedInstructionCount = 0;
            
            /* Iterating through each instruction */
            for( analysis::DataflowGraph::InstructionVector::const_iterator instruction = basicBlock->instructions().begin();
                instruction != basicBlock->instructions().end(); ++instruction)
            {
                ir::PTXInstruction *ptxInstruction = (ir::PTXInstruction *)instruction->i;
                if(instrumentationConditionsMet(*ptxInstruction, translationBlock)) 
                {
                    attributes.basicBlockInstructionCount++;
                    
                    if(translationBlock.specifier.checkForPredication && 
                        (ptxInstruction->pg.condition == ir::PTXOperand::Pred || ptxInstruction->pg.condition == ir::PTXOperand::InvPred))
                    {
                        bool isPredInstCount = false;
                        ir::PTXOperand guard;
                    
                        ir::PTXKernel::PTXStatementVector::iterator position = translationBlock.statements.end();
                        for(ir::PTXKernel::PTXStatementVector::iterator statement = translationBlock.statements.begin();
                            statement != translationBlock.statements.end(); ++statement) { 
                            
                            ir::PTXOperand ir::PTXInstruction::* source_operands[] = 
                                { &ir::PTXInstruction::a, & ir::PTXInstruction::b, & ir::PTXInstruction::c};

                            for (int i = 0; i < 3; i++) 
                            {
                                ir::PTXOperand &operand = statement->instruction.*(source_operands[i]);
                                
                                if(operand.identifier == BASIC_BLOCK_EXEC_INST_COUNT)
                                {
                                    position = statement;
                                    break;
                                }
                                else if(operand.identifier == BASIC_BLOCK_PRED_INST_COUNT)
                                {
                                    position = statement;
                                    isPredInstCount = true;
                                    guard = statement->instruction.pg;
                                    break;
                                }
                            }   
                        }             
        
                        if(position == translationBlock.statements.end())
                            throw hydrazine::Exception("Unable to locate BASIC_BLOCK_EXEC_INST_COUNT or BASIC_BLOCK_PRED_INST_COUNT statement!");
                    
                        analysis::DataflowGraph::RegisterId predCount = dfg().newRegister();
                        ir::PTXOperand::DataType type = (sizeof(size_t) == 8 ? ir::PTXOperand::u64: ir::PTXOperand::u32);
                
                        if(isPredInstCount)
                        {
                            ir::PTXInstruction ballot(ir::PTXInstruction::Vote);
                            ballot.vote = ir::PTXInstruction::Ballot;
                            ballot.type = ir::PTXOperand::b32;
                            
                            ballot.d.type = ir::PTXOperand::b32;
                            ballot.d.addressMode = ir::PTXOperand::Register;
                            ballot.d.reg = predCount;
                            ballot.a.addressMode = ir::PTXOperand::Register;
                            ballot.a.type = ir::PTXOperand::pred;
                            ballot.a = ptxInstruction->pg;
                            
                            ballot.pg = guard;
                            ballot.pg.condition = ir::PTXOperand::Pred;

                            ir::PTXInstruction popc(ir::PTXInstruction::Popc);
                            popc.type = popc.a.type = ir::PTXOperand::b32;
                            popc.d.type = ir::PTXOperand::u32;
                            popc.d.addressMode = popc.a.addressMode = ir::PTXOperand::Register;
                            popc.a = ballot.d;   
                            analysis::DataflowGraph::RegisterId popcResult = dfg().newRegister();
                            popc.d.reg = popcResult;
                            
                            popc.pg = guard;
                            popc.pg.condition = ir::PTXOperand::Pred;

                            ir::PTXInstruction cvt(ir::PTXInstruction::Cvt);
                            cvt.type = cvt.d.type = type;
                            cvt.a.type = ir::PTXOperand::u32;
                            cvt.d.addressMode = cvt.a.addressMode = ir::PTXOperand::Register;
                            cvt.a = popc.d;   
                            analysis::DataflowGraph::RegisterId cvtResult = dfg().newRegister();
                            cvt.d.reg = cvtResult;
                            
                            cvt.pg = guard;
                            cvt.pg.condition = ir::PTXOperand::Pred;

                            ir::PTXInstruction add(ir::PTXInstruction::Add);
                            add.type = add.d.type = add.a.type = add.b.type = type;
                            add.d.addressMode = add.a.addressMode = add.b.addressMode = ir::PTXOperand::Register;
                            
                            add.d = add.a = position->instruction.d;
                            add.b.reg = cvtResult;
                            
                            add.pg = guard;
                            add.pg.condition = ir::PTXOperand::Pred;
                            
                            ir::PTXStatement stmt(ir::PTXStatement::Instr);
                            stmt.instruction = ballot;
                            position = translationBlock.statements.insert(position + 1, stmt);
                            stmt.instruction = popc;
                            position = translationBlock.statements.insert(position + 1, stmt);
                            stmt.instruction = cvt;
                            position = translationBlock.statements.insert(position + 1, stmt);
                            stmt.instruction = add;
                            position = translationBlock.statements.insert(position + 1, stmt);
                            
                        }
                        else
                        {   
                            ir::PTXInstruction selp(ir::PTXInstruction::SelP);
                            selp.type = selp.d.type = selp.b.type = selp.a.type = selp.c.type = type;
                            selp.d.addressMode = selp.c.addressMode = ir::PTXOperand::Register;
                            selp.a.addressMode = selp.b.addressMode = ir::PTXOperand::Immediate;
                            
                            selp.d.reg = predCount;
                            selp.b.imm_int = 0;
                            selp.a.imm_int = 1;
                            selp.c = ptxInstruction->pg;

                            ir::PTXInstruction add(ir::PTXInstruction::Add);
                            add.type = add.d.type = add.a.type = add.b.type = type;
                            add.d.addressMode = add.a.addressMode = add.b.addressMode = ir::PTXOperand::Register;
                            
                            add.d = add.a = position->instruction.d;
                            add.b.reg = predCount;
                            

                            ir::PTXStatement stmt(ir::PTXStatement::Instr);
                            stmt.instruction = selp;
                            position = translationBlock.statements.insert(position + 1, stmt);
                            stmt.instruction = add;
                            translationBlock.statements.insert(position + 1, stmt);
                        }
                    
                    }
                    else
                    {
                        attributes.basicBlockExecutedInstructionCount++;
                    }
                }        
            }
            
            
            unsigned int loc = 0;
            
            /* if we are inserting at the end of the basic block, make sure to insert just before the last statement, which is most likely a
                control-flow statement */
            if(translationBlock.label == EXIT_BASIC_BLOCK)
                loc = basicBlock->instructions().size() - 1;
            
            insertBefore(translationBlock, attributes, basicBlock, loc);
            
            if(translationBlock.specifier.checkForPredication)
            {
                for(ir::PTXKernel::PTXStatementVector::iterator statement = translationBlock.statements.begin();
                statement != translationBlock.statements.end(); ++statement) {
                
                    if( statement->instruction.opcode == ir::PTXInstruction::SelP && 
                        (statement+1)->instruction.opcode == ir::PTXInstruction::Add)
                    {
                        translationBlock.statements.erase(statement, statement + 2);
                    } 
                    
                    if( statement->instruction.opcode == ir::PTXInstruction::Vote && 
                        (statement+1)->instruction.opcode == ir::PTXInstruction::Popc && 
                        (statement+2)->instruction.opcode == ir::PTXInstruction::Cvt &&
                        (statement+3)->instruction.opcode == ir::PTXInstruction::Add)
                    {
                        translationBlock.statements.erase(statement, statement + 4);
                    } 
                
                }
            }
            
            attributes.basicBlockId++;          
        }    
    }

    void CToPTXInstrumentationPass::instrumentKernel(TranslationBlock translationBlock) 
    {
        StaticAttributes attributes;
        attributes.basicBlockId = 0;
        attributes.instructionId = 0;
        attributes.kernelInstructionCount = 0;
        attributes.basicBlockCount = dfg().size() - 2;
                
        /* ensure that there is at least one basic block -- otherwise, skip this kernel */
        if(dfg().empty())
            return;
        
        /* by default, insert each statement to the beginning of the kernel */
	    analysis::DataflowGraph::iterator block = dfg().begin();
	    ++block;
	    
	    attributes.kernelInstructionCount = kernelInstructionCount(translationBlock);
	    
	    unsigned int loc = 0;
        
        /* if inserting at the end of the kernel, insert right before the last statement in the kernel, which is most likely
            an exit or return instruction */
        if(translationBlock.label == EXIT_KERNEL) {    
            block = --(dfg().end());
            while(block->instructions().size() == 0) {
                block--;
            }
            loc = block->instructions().size() - 1;
        }
        
	    insertBefore(translationBlock, attributes, block, loc);
    }
    
    unsigned int CToPTXInstrumentationPass::kernelInstructionCount(TranslationBlock translationBlock)
    {
        analysis::DataflowGraph::iterator block = dfg().begin();
	    ++block;
	    
	    unsigned long kernelInstructionCount = 0;
	    
	    for( analysis::DataflowGraph::iterator basicBlock = block; 
            basicBlock != dfg().end(); ++basicBlock )
        {
            if(basicBlock->instructions().empty())
              continue;
            
            /* Iterating through each instruction */
            for( analysis::DataflowGraph::InstructionVector::const_iterator instruction = basicBlock->instructions().begin();
                instruction != basicBlock->instructions().end(); ++instruction)
            {
                ir::PTXInstruction *ptxInstruction = (ir::PTXInstruction *)instruction->i;
                if(instrumentationConditionsMet(*ptxInstruction, translationBlock))
                    kernelInstructionCount++;
            }
        }
        
        return kernelInstructionCount;
    
    }

    ir::PTXStatement CToPTXInstrumentationPass::computeBaseAddress(ir::PTXStatement statement, ir::PTXInstruction original)
    {
        ir::PTXStatement toInsert = statement;
        
        if(statement.instruction.a.identifier == COMPUTE_BASE_ADDRESS)
            {
                toInsert.instruction.d.reg = newRegisterMap[COMPUTE_BASE_ADDRESS];
                toInsert.instruction.a.reg = newRegisterMap[COMPUTE_BASE_ADDRESS];
                toInsert.instruction.b.reg = newRegisterMap[statement.instruction.b.identifier];
                toInsert.instruction.d.identifier.clear();
                toInsert.instruction.a.identifier.clear();
                toInsert.instruction.b.identifier.clear();
                return toInsert;
            }
        
            toInsert.instruction.d.reg = dfg().newRegister();
            newRegisterMap[toInsert.instruction.d.identifier] = toInsert.instruction.d.reg;
            toInsert.instruction.d.identifier.clear();
            
            if(original.opcode == ir::PTXInstruction::St)
            {
                if(original.d.addressMode == ir::PTXOperand::Indirect)
                {
                    toInsert.instruction.a.identifier.clear();
                    toInsert.instruction.b.identifier.clear();
                    toInsert.instruction.opcode = ir::PTXInstruction::Add;
                    toInsert.instruction.a.addressMode = ir::PTXOperand::Register;
                    toInsert.instruction.a.reg = original.d.reg;   
                    toInsert.instruction.b.addressMode = ir::PTXOperand::Immediate;
                    toInsert.instruction.b.imm_int = original.d.offset;   
                }
            }
            else if(original.opcode == ir::PTXInstruction::Ld)
            {
                if(original.a.addressMode == ir::PTXOperand::Indirect)
                {
                    toInsert.instruction.opcode = ir::PTXInstruction::Add;
                    toInsert.instruction.a.addressMode = ir::PTXOperand::Register;
                    toInsert.instruction.a.reg = original.a.reg;   
                    toInsert.instruction.b.addressMode = ir::PTXOperand::Immediate;
                    toInsert.instruction.b.imm_int = original.a.offset;   
                }
            
            }
    
            return toInsert;
    }


/*******************************************************************************************

    runOnKernel

********************************************************************************************/

    void CToPTXInstrumentationPass::runOnKernel( ir::IRKernel & k)
	{
		report("CToPTXInstrumentationPass::runOnKernel");
		report("DFG (before erase) begin....");
		std::ostream out(NULL);
		dfg().writeLite(out);
		report("DFG (before erase) end....");
	    std::vector<TranslationBlock> translationBlocks;

        optimize(translation.statements);
	   
	    for(translator::CToPTXData::RegisterVector::const_iterator reg = translation.registers.begin();
	        reg != translation.registers.end(); ++reg) {
	        newRegisterMap[*reg] = dfg().newRegister();   
	    }
        
        TranslationBlock initialBlock;
        
        for(ir::PTXKernel::PTXStatementVector::const_iterator statement = translation.statements.begin();
            statement != translation.statements.end(); ++statement) {
            
            if(statement->directive == ir::PTXStatement::Param)
            {   
                k.insertParameter(ir::Parameter(*statement, false, false), true);
                continue;
            }
            
            if(statement->directive == ir::PTXStatement::Shared)
            {
                ir::PTXKernel *ptxKernel = (ir::PTXKernel *)&k;
                ptxKernel->locals.insert( 
                    std::make_pair( statement->name, ir::Local( *statement )));
                continue;
            }
            
            /* check if predication is enabled */
            if(statement->directive == ir::PTXStatement::Instr)
            {
                ir::PTXOperand ir::PTXInstruction::* source_operands[] = 
                    { &ir::PTXInstruction::a, & ir::PTXInstruction::b, & ir::PTXInstruction::c};

                for (int i = 0; i < 3; i++) 
                {
                    ir::PTXOperand operand = statement->instruction.*(source_operands[i]);
                    
                    if(operand.identifier == BASIC_BLOCK_EXEC_INST_COUNT ||
                        operand.identifier == BASIC_BLOCK_PRED_INST_COUNT)
                    {
                        if(translationBlocks.size() > 0)
                        {
                            transforms::TranslationBlock last = translationBlocks.back();
                            last.specifier.checkForPredication = true;
                            translationBlocks.pop_back();
                            translationBlocks.push_back(last);
                        }
                        else {
                            initialBlock.specifier.checkForPredication = true;
                        }
                        break;
                    }
                }
            }
            
        
            /* check if a label was encountered */
            if(statement->directive == ir::PTXStatement::Label) {
                
                /* if the label specifies the instruction class, update the instrumentation specifier in the 
                    most recently inserted translation block with this information */        
                for(std::vector<std::string>::const_iterator instClass = instructionClasses.begin(); instClass != instructionClasses.end();
                    instClass++)
                {
                    if(statement->name == *instClass){
                        
                        if(translationBlocks.size() > 0){
                            transforms::TranslationBlock last = translationBlocks.back();
                            last.specifier.instructionClassVector.push_back(*instClass);
                            translationBlocks.pop_back();
                            translationBlocks.push_back(last);
                        }
                        else {
                            initialBlock.specifier.instructionClassVector.push_back(*instClass);
                        }
                        break;
                    }
                }
                
                /* if the label specifies the address space, update the instrumentation specifier in the 
                    most recently inserted translation block with this information */     
                for(std::vector<std::string>::const_iterator addressSpace = addressSpaceSpecifiers.begin(); addressSpace != addressSpaceSpecifiers.end();
                    addressSpace++)
                {
                    if(statement->name == *addressSpace){
                        
                        if(translationBlocks.size() > 0){
                            transforms::TranslationBlock last = translationBlocks.back();
                            last.specifier.addressSpaceVector.push_back(*addressSpace);
                            translationBlocks.pop_back();
                            translationBlocks.push_back(last);
                        }
                        else {
                            initialBlock.specifier.addressSpaceVector.push_back(*addressSpace);
                        }
                        break;
                    }
                }
                
                /* if the label specifies the type, update the instrumentation specifier in the 
                    most recently inserted translation block with this information */     
                for(std::vector<std::string>::const_iterator type = types.begin(); type != types.end();
                    type++)
                {
                    if(statement->name == *type){

                        if(translationBlocks.size() > 0){
                            transforms::TranslationBlock last = translationBlocks.back();
                            last.specifier.dataTypeVector.push_back(*type);
                            translationBlocks.pop_back();
                            translationBlocks.push_back(last);
                        }
                        else {
                            initialBlock.specifier.dataTypeVector.push_back(*type);
                        }
                        break;
                    }
                
                }
                /* if an instrumentation target specifier is found, start a new translation block */
                if(statement->name == ENTER_KERNEL || statement->name == EXIT_KERNEL || statement->name == ENTER_BASIC_BLOCK || statement->name == EXIT_BASIC_BLOCK ||
                    statement->name == ON_INSTRUCTION) {
                    
                    TranslationBlock translationBlock;
                    translationBlock.label = translationBlock.specifier.id = statement->name;
                    translationBlock.target = TranslationBlock::KERNEL;
                    
                    if(statement->name == ON_INSTRUCTION)
                    {
                        translationBlock.target = TranslationBlock::INSTRUCTION;
                    }
                    else if(statement->name == ENTER_BASIC_BLOCK || statement->name == EXIT_BASIC_BLOCK)
                    {
                        translationBlock.target = TranslationBlock::BASIC_BLOCK;
                    }
                    
                    translationBlocks.push_back(translationBlock);
                  
                }
                
            }
            
            /* insert each statement into the most recently inserted translation block */
            if(translationBlocks.size() > 0){
                transforms::TranslationBlock last = translationBlocks.back();
                last.statements.push_back(*statement);
                translationBlocks.pop_back();
                translationBlocks.push_back(last);
            }
            /* if no translation blocks have been added, insert the statement into the initial block */
            else {
                initialBlock.statements.push_back(*statement);
            }
        }
        
        unsigned int j = 0;
        for(j = 0; j < translationBlocks.size(); j++)
        {
        
            for(std::vector<std::string>::const_iterator instClass = initialBlock.specifier.instructionClassVector.begin(); 
            instClass != initialBlock.specifier.instructionClassVector.end(); instClass++)
                    translationBlocks.at(j).specifier.instructionClassVector.push_back(*instClass);
                    
            for(std::vector<std::string>::const_iterator addressSpace = initialBlock.specifier.addressSpaceVector.begin(); 
            addressSpace != initialBlock.specifier.addressSpaceVector.end(); addressSpace++)
                    translationBlocks.at(j).specifier.addressSpaceVector.push_back(*addressSpace);
            
            for(std::vector<std::string>::const_iterator type = initialBlock.specifier.dataTypeVector.begin(); 
            type != initialBlock.specifier.dataTypeVector.end(); type++)
                    translationBlocks.at(j).specifier.dataTypeVector.push_back(*type);
        
            switch(translationBlocks.at(j).target){
                case TranslationBlock::INSTRUCTION:
                    instrumentInstruction(translationBlocks.at(j));
                break;
                case TranslationBlock::BASIC_BLOCK:
                    instrumentBasicBlock(translationBlocks.at(j));
                break;
                case TranslationBlock::KERNEL:
                    instrumentKernel(translationBlocks.at(j));
                break;
                default:
                    instrumentKernel(translationBlocks.at(j));
                break;
            }
        }
		report("DFG (after erase) begin....");
		dfg().writeLite(out);
		report("DFG (after erase) end....");
        
            
        /* insert initial translation block at the beginning of the kernel -- this is the default case */
        instrumentKernel(initialBlock);
	}
	
    void CToPTXInstrumentationPass::initialize(const ir::Module& m )
	{   
	    
	}

    void CToPTXInstrumentationPass::finalize( )
	{
	
	}
	
	
	void CToPTXInstrumentationPass::optimize(ir::PTXKernel::PTXStatementVector & statements)
	{
	    ir::PTXKernel::PTXStatementVector toErase;
	    bool propagateConstant = true;
	    bool saveOperands = false;
	    
	    ir::PTXOperand d, a, b, c;
	
	    for(ir::PTXKernel::PTXStatementVector::iterator statement = statements.begin();
            statement != statements.end(); ++statement) 
        {
        
            if(statement->instruction.opcode == ir::PTXInstruction::Mov && 
                statement->instruction.a.addressMode == ir::PTXOperand::Immediate)
            {
                propagateConstant = true;
                for(FunctionNameVector::const_iterator function = functionNames.begin();
                    function != functionNames.end(); ++function)
                {
                    if(statement->instruction.d.identifier == *function)
                    {
                        propagateConstant = false;
                        break;
                    }
                }

                if(propagateConstant)
                {
                    constants[statement->instruction.d.identifier] = statement->instruction.a.imm_uint;
                    toErase.push_back(*statement);
                }
            }       
            
            if( statement->instruction.opcode == ir::PTXInstruction::Mad ||
                statement->instruction.opcode == ir::PTXInstruction::Add ||
                statement->instruction.opcode == ir::PTXInstruction::Sub ||
                statement->instruction.opcode == ir::PTXInstruction::Div ||
                statement->instruction.opcode == ir::PTXInstruction::Mul ||
                statement->instruction.opcode == ir::PTXInstruction::Shr ||
                statement->instruction.opcode == ir::PTXInstruction::Shl ||
                statement->instruction.opcode == ir::PTXInstruction::SetP)
            {    
                ir::PTXOperand ir::PTXInstruction::* source_operands[] = 
                    { &ir::PTXInstruction::a, & ir::PTXInstruction::b, & ir::PTXInstruction::c};

                for (int i = 0; i < 3; i++) 
                {
                    ir::PTXOperand &operand = statement->instruction.*(source_operands[i]);
        
                    if( constants.find(operand.identifier) != constants.end())
                    {
                        (statement->instruction.*(source_operands[i])).addressMode = ir::PTXOperand::Immediate;
                        (statement->instruction.*(source_operands[i])).imm_uint = constants[operand.identifier];
                    }
                }
            }
            
            if(statement->instruction.opcode == ir::PTXInstruction::Mul && 
                (statement+1)->instruction.opcode == ir::PTXInstruction::Add &&
                statement->instruction.d.identifier == (statement+1)->instruction.d.identifier &&
                statement->instruction.d.identifier == (statement+1)->instruction.a.identifier)
                {
                    statement->instruction.opcode = ir::PTXInstruction::Mad;
                    statement->instruction.c.addressMode = (statement+1)->instruction.b.addressMode;
                    statement->instruction.c.identifier = (statement+1)->instruction.b.identifier;
                    toErase.push_back(*(statement+1));
                    
                    if((statement + 2)->instruction.opcode == ir::PTXInstruction::Ld)
                    {
                        d = statement->instruction.d;
                        a = statement->instruction.a;
                        b = statement->instruction.b;
                        c = statement->instruction.c;
                        
                        saveOperands = true;
                    }
                    else if(saveOperands &&
                        (statement + 2)->instruction.opcode == ir::PTXInstruction::St)
                    {
                        (statement + 2)->instruction.d.identifier = d.identifier;
                        toErase.push_back(*statement);
                        saveOperands = false;
                    }
                }            
        }	
        
       
        ir::PTXKernel::PTXStatementVector newStatementsVector;
        bool add = true;
            
        for(ir::PTXKernel::PTXStatementVector::iterator statement = statements.begin();
        statement != statements.end(); ++statement) 
        {
            add = true;
            
            for(ir::PTXKernel::PTXStatementVector::iterator 
                s = toErase.begin(); s != toErase.end(); ++s)
                { 
                    if(statement->toString() == s->toString())
                    {
                        add = false;
                        break;
                    }        
                }
            
            if(add)       
                newStatementsVector.push_back(*statement);                 
        }
        
        statements = newStatementsVector;

	}
	
    CToPTXInstrumentationPass::CToPTXInstrumentationPass(translator::CToPTXData
        translation)
		: KernelPass(Analysis::StringVector({"DataflowGraphAnalysis"}), "CToPTXInstrumentationPass" ),
        translation(translation)
	{
	    report("CToPTXInstrumentationPass::CToPTXInstrumentationPass...");
	    parameterMap = translation.parameterMap;
	 
	    functionNames = { COMPUTE_BASE_ADDRESS, GET_PREDICATE_VALUE };
	 
	    instructionClasses = { ON_MEM_READ, ON_MEM_WRITE, ON_PREDICATED, ON_BRANCH, ON_CALL, ON_BARRIER, ON_ATOMIC, ON_ARITH_OP, ON_FLOATING_POINT, ON_TEXTURE };
	    addressSpaceSpecifiers = { GLOBAL, LOCAL, SHARED, CONST, PARAM, TEXTURE };
	    types = { TYPE_INT, TYPE_FP };
	    
	    opcodeMap[ir::PTXInstruction::Ld] = ON_MEM_READ;
	    opcodeMap[ir::PTXInstruction::St] = ON_MEM_WRITE;
	    opcodeMap[ir::PTXInstruction::Bra] = ON_BRANCH;
	    opcodeMap[ir::PTXInstruction::Call] = ON_CALL;
	    opcodeMap[ir::PTXInstruction::Bar] = ON_BARRIER;
	    opcodeMap[ir::PTXInstruction::Membar] = ON_BARRIER;
	    opcodeMap[ir::PTXInstruction::Atom] = ON_ATOMIC;
	    opcodeMap[ir::PTXInstruction::Abs] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Add] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::AddC] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Bfe] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Bfi] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Bfind] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Brev] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Clz] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Cos] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Div] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Mad24] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Max] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Min] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Mul24] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Mul] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Popc] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Prmt] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Sad] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Abs] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Rem] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Sqrt] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Sin] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Sub] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::SubC] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::Neg] = ON_ARITH_OP;
	    opcodeMap[ir::PTXInstruction::TestP] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::CopySign] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Lg2] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Ex2] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Fma] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Rcp] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Sqrt] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Rsqrt] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Sin] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Cos] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Tex] = ON_FLOATING_POINT;
	    opcodeMap[ir::PTXInstruction::Tld4] = ON_TEXTURE;
	    opcodeMap[ir::PTXInstruction::Txq] = ON_TEXTURE;
	    
	    addressSpaceMap[GLOBAL] = ir::PTXInstruction::Global;
	    addressSpaceMap[LOCAL] = ir::PTXInstruction::Local;
	    addressSpaceMap[SHARED] = ir::PTXInstruction::Shared;
	    addressSpaceMap[CONST] = ir::PTXInstruction::Const;
	    addressSpaceMap[PARAM] = ir::PTXInstruction::Param;
	    addressSpaceMap[TEXTURE] = ir::PTXInstruction::Texture;
	   
	    dataTypeMap[ir::PTXOperand::s8] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::s16] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::s32] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::s64] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::u8] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::u16] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::u32] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::u64] = TYPE_INT;
	    dataTypeMap[ir::PTXOperand::f16] = TYPE_FP;
	    dataTypeMap[ir::PTXOperand::f32] = TYPE_FP;
	    dataTypeMap[ir::PTXOperand::f64] = TYPE_FP;
	}
	
	InstrumentationSpecifier::InstrumentationSpecifier()
	    : checkForPredication(false), isPredicated(false)
	    {
	    }
}


#endif

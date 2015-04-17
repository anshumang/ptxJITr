/*! \file YieldInstrumentor.h
	\date Wednesday April 8, 2015
	\author Anshuman Goswami <anshumang@cc.gatech.edu>
	\brief The source file for YieldInstrumentor
*/

#include <lynx/instrumentation/interface/YieldInstrumentor.h>
#include <fstream>

namespace instrumentation
{

   bool YieldInstrumentor::validate() {
       std::cout << "YieldInstrumentor::validate()" << std::endl;
       return true;
   }

   void YieldInstrumentor::analyze(ir::Module &module) {
       //no static analysis necessary
   }

   void YieldInstrumentor::initialize() {
       //Empty for now - the per SM launch start timestamps array and time allotted variable should be allocated and initialized here
   }

   void YieldInstrumentor::extractResults(std::ostream *out) {
       //No profiling results to obtain for now
   } 

    std::string YieldInstrumentor::specificationPath() {
        
        passes.clear();
        std::string resource;

        resource = "resources/Yield.c";

        return resource;

    }

    YieldInstrumentor::YieldInstrumentor()
    {
        std::cout << "YieldInstrumentor CTOR" << std::endl;
    }

}

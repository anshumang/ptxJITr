/*! \file YieldInstrumentor.h
	\date Wednesday April 8, 2015
	\author Anshuman Goswami <anshumang@cc.gatech.edu>
	\brief The header file for YieldInstrumentor
*/

#ifndef YIELD_INSTRUMENTOR_H_INCLUDED
#define YIELD_INSTRUMENTOR_H_INCLUDED

#include <string>
#include <map>
#include <algorithm>

#include <lynx/instrumentation/interface/PTXInstrumentor.h>

#include <ocelot/ir/interface/Module.h>
#include <ocelot/transforms/interface/Pass.h>

namespace instrumentation
{
   class YieldInstrumentor : public PTXInstrumentor
   {
         public:
         YieldInstrumentor();

         bool validate();

         void initialize();

         void analyze(ir::Module &module);

         void extractResults(std::ostream *out);

         std::string specificationPath();

    };
}

#endif

#pragma once
#include "../../source-format/GlobalContext.h"
// Carbon's always-resident section is a context bank: profile presentation,
// native sky variants and background animation templates coexist there.
// It is not an unconditional scenery section. Its original scene and all
// model payloads remain in the source pack; native sky/background adapters
// own their activation, independently of ordinary streamed terrain pages.
static void ValidateSourceGlobalContext(){
 auto e=v2Store.find(23,"scenes/26000");
#ifdef W2C_PUBLIC_BUILD
 auto instances=w2source::validateGlobalContext(e.meta,v2Store.read(e),"world-stream");
#else
 auto instances=w2source::validateGlobalContext(e.meta,v2Store.read(e));
#endif
 Log("SOURCE_GLOBAL_CONTEXT preserved_instances=%u unconditional_terrain=0 native_sky_and_motion_activation=1",instances);
}

#pragma once
static bool renderWorkProfiling=false;
static double renderFrameQueryMs=0,renderFrameSubmitMs=0;
static unsigned renderFrameQueries=0,renderFramePasses=0;
struct RenderQueryTimer {
 double start=renderWorkProfiling?Clock():0;
 ~RenderQueryTimer(){if(start){renderFrameQueryMs+=Clock()-start;++renderFrameQueries;}}
};
struct NeoPhaseTrace {
 double previous=renderWorkProfiling?Clock():0;
 std::array<double,8> ms{};
 void Mark(unsigned phase){if(previous){double now=Clock();ms[phase]+=now-previous;previous=now;}}
};

static double renderFrameCollisionMs=0,renderFrameShadowMs=0,renderFrameStateMs=0;
static unsigned renderFrameCollisionQueries=0;
static double renderFrameHeliMs=0;static unsigned renderFrameHeliQueries=0;
struct HeliClearanceWorkTimer {
 double start=renderWorkProfiling?Clock():0;
 ~HeliClearanceWorkTimer(){if(start){renderFrameHeliMs+=Clock()-start;++renderFrameHeliQueries;}}
};
struct NativeCollisionWorkTimer {
 double start=renderWorkProfiling?Clock():0;
 ~NativeCollisionWorkTimer(){if(start){renderFrameCollisionMs+=Clock()-start;++renderFrameCollisionQueries;}}
};

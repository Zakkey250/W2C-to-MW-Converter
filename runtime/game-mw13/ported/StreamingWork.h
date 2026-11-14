#pragma once
// CPU-only queue selection. Each producer has a bounded completion queue;
// round-robin prevents a continuous texture/prop stream starving terrain.
enum class StreamJob {None, Terrain, Texture, Prop};
static StreamJob PickStreamJob(bool terrain,bool texture,bool prop,unsigned& cursor){
 const bool available[3]{terrain,texture,prop};
 for(unsigned n=0;n<3;++n){unsigned index=(cursor+n)%3;if(available[index]){cursor=(index+1)%3;return static_cast<StreamJob>(index+1);}}
 return StreamJob::None;
}
struct SceneUploadBudget {
 double start=0,limit=0;
 void Begin(double now,bool initial){start=now;limit=initial?8:2;}
 bool Available(double now)const{return now-start<limit;}
};

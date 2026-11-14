#pragma once
#include <string_view>
namespace world_radar {
enum class Source { Pending, NativeCourse, WorldRoaming, OtherCourse };
inline Source Identify(unsigned mode,std::string_view prefix){
 if(prefix.empty())return Source::Pending;
 const bool full=prefix=="MINI_MAP"||prefix=="MINI_MAP_UNLOCK_1"||prefix=="MINI_MAP_UNLOCK_2";
 // Full-world resources are also used by native pursuit activities and
 // start-only police missions. Their atlas does not become a course map
 // merely because GRaceStatus is in race mode.
 if((mode==0||mode==1)&&full)return Source::WorldRoaming;
 if(prefix.starts_with("MINI_MAP_")&&!full)return prefix.size()>9&&prefix[9]>='0'&&prefix[9]<='9'?Source::NativeCourse:Source::OtherCourse;
 return Source::NativeCourse;
}
struct Calibration {float x,y,width;unsigned short rotation;bool operator==(const Calibration&)const=default;};
// Replaced from the approved original-map calibration before world activation.
inline Calibration World{};
struct State {
 Calibration original{};bool owns=false,hasOriginal=false;
 // Restore only our exact write. Another event/MOD remains the owner of its
 // own coordinates; its write must never become our native baseline.
 bool Update(Source source,Calibration& current){
  if(source!=Source::WorldRoaming){
   if(!owns)return false;
   owns=false;if(current!=World)return false;
   current=original;return true;
  }
  if(current==World){if(hasOriginal)owns=true;return false;}
  original=current;hasOriginal=true;owns=true;current=World;return true;
 }
};
}

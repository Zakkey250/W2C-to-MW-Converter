#pragma once
// Shared read-only physical position for world services, independent of NeoAI.
static bool W2CReadVehiclePosition(BYTE* vehicle,Vec3& position){
 __try{
  if(!vehicle)return false;auto entity=vehicle-0x80;auto table=*reinterpret_cast<void***>(entity);
  if(!table||!table[21])return false;
  auto body=reinterpret_cast<void*(__thiscall*)(void*)>(table[21])(entity);if(!body)return false;
  auto bt=*reinterpret_cast<void***>(body);if(!bt||!bt[8])return false;
  auto actual=reinterpret_cast<const float*(__thiscall*)(void*)>(bt[8])(body);if(!actual)return false;
  memcpy(&position,actual,12);
  return std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z)&&
   fabs(position.x)<100000&&fabs(position.y)<100000&&fabs(position.z)<100000;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool W2CReadPlayerPosition(Vec3& position){
 __try{
  auto vehicles=*reinterpret_cast<BYTE***>(0x92CD1C);unsigned count=*reinterpret_cast<unsigned*>(0x92CD24);
  if(!vehicles||count>128||count>*reinterpret_cast<unsigned*>(0x92CD20))return false;
  for(unsigned i=0;i<count;++i){auto vehicle=vehicles[i];
   if(vehicle&&*reinterpret_cast<unsigned*>(vehicle)==0x8AA828&&*reinterpret_cast<unsigned*>(vehicle+0x94)==0&&W2CReadVehiclePosition(vehicle,position))return true;}
  return false;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

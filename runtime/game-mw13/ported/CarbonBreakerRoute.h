#pragma once
// Two source parking booths are the area-stimulus owners for the two crane
// loads. Never reroute other tollbooths or a different catalog record at an ID.
static constexpr bool CarbonBreakerCraneBoothRoute(unsigned id,unsigned engine,
 unsigned primary,unsigned secondary,unsigned parameter){
 return (id==7743||id==8151)&&engine==0x919191C6&&primary==0x97B3128F&&
  secondary==0x97B3128F&&parameter==0x5AEFC6A8;
}

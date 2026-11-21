#pragma once
#include "BoundedDisk.h"
namespace w2road {
namespace d=w2v2disk;
struct Point {float x=0,y=0,z=0;};
struct Volume {Point p[3];float height[3]{};};
struct Cover {bool tunnel=false;float floor=0,roof=0;};
struct Environment {
 std::vector<Volume> volumes;
 std::map<std::pair<int,int>,std::vector<unsigned>> grid;
 float groundOrigin(Point p,float origin)const{
  const auto it=grid.find({int(std::floor(p.x/64)),int(std::floor(p.y/64))});if(it==grid.end())return origin;
  for(auto index:it->second){const auto& v=volumes[index];const auto a=v.p[0],b=v.p[1],c=v.p[2];
   const double det=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
   const double u=((p.x-a.x)*(c.y-a.y)-(p.y-a.y)*(c.x-a.x))/det;
   const double w=((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x))/det;
   if(u<-.0001||w<-.0001||u+w>1.0001)continue;
   const float floor=float(a.z+(b.z-a.z)*u+(c.z-a.z)*w);
   // Native CARP has no bank height per lane. Recover only small penetration
   // into an authored pavement; a separate deck remains outside this band.
   if(floor>p.z&&floor<=p.z+2.5f)origin=std::max(origin,floor+.03f);
  }return origin;
 }
 Cover at(Point p)const{
  const auto it=grid.find({int(std::floor(p.x/64)),int(std::floor(p.y/64))});if(it==grid.end())return {};
  for(auto index:it->second){const auto& v=volumes[index];const auto a=v.p[0],b=v.p[1],c=v.p[2];
   const double det=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
   const double u=((p.x-a.x)*(c.y-a.y)-(p.y-a.y)*(c.x-a.x))/det;
   const double w=((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x))/det;
   if(u<-.0001||w<-.0001||u+w>1.0001)continue;
   const auto floor=float(a.z+(b.z-a.z)*u+(c.z-a.z)*w);
   const auto roof=float(floor+v.height[0]+(v.height[1]-v.height[0])*u+(v.height[2]-v.height[0])*w);
   if(p.z>=floor-.3f&&p.z<roof-.03f)return {true,floor,roof};
  }return {};
 }
};
inline Environment read(const std::filesystem::path& path,const std::string& generation,const std::string& collision){
 Environment result;if(!std::filesystem::exists(path))return result;
 d::File file(path);const auto doc=d::parse(file.read(0,file.size,4*d::MiB),4*d::MiB);
 d::keys(doc,{"schema","generation","collisionAddonSha256","volumes"});
 d::need(d::uintv(doc.at("schema"))==1&&doc.at("generation")==generation&&doc.at("collisionAddonSha256")==collision,"road environment identity");
 const auto& rows=doc.at("volumes");d::need(rows.is_array()&&rows.size()<=16384,"road tunnel volume count");d::U references=0;
 for(const auto& row:rows){d::keys(row,{"floor","height"});const auto& points=row.at("floor");const auto& heights=row.at("height");
  d::need(points.is_array()&&points.size()==3&&heights.is_array()&&heights.size()==3,"road tunnel volume shape");Volume v;
  for(unsigned i=0;i<3;++i){d::vec(points[i],3);v.p[i]={float(d::number(points[i][0])),float(d::number(points[i][1])),float(d::number(points[i][2]))};
   d::need(std::abs(v.p[i].x)<30000&&std::abs(v.p[i].y)<30000&&std::abs(v.p[i].z)<30000,"road tunnel position");v.height[i]=float(d::number(heights[i]));d::need(v.height[i]>=3.5f&&v.height[i]<=100,"road tunnel clearance");}
  auto a=v.p[0],b=v.p[1],c=v.p[2];d::need(std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))>1e-5,"degenerate road tunnel floor");
  int x0=int(std::floor(std::min({a.x,b.x,c.x})/64)),x1=int(std::floor(std::max({a.x,b.x,c.x})/64));
  int y0=int(std::floor(std::min({a.y,b.y,c.y})/64)),y1=int(std::floor(std::max({a.y,b.y,c.y})/64));
  references+=d::U(x1-x0+1)*d::U(y1-y0+1);d::need(references<=262144,"road tunnel spatial index budget");
  const auto index=unsigned(result.volumes.size());for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y)result.grid[{x,y}].push_back(index);result.volumes.push_back(v);
 }return result;
}
}

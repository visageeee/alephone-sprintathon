#include "../Source_Files/zpc_import.h"
#include <fstream>
#include <iostream>
#include <cassert>
using namespace zpc_import;
Bytes read(const char* p) { std::ifstream f(p,std::ios::binary); assert(f); return Bytes(std::istreambuf_iterator<char>(f),{}); }
void write(const char* p,const Bytes& b) { std::ofstream f(p,std::ios::binary); f.write((const char*)b.data(),b.size()); assert(f); }
template<class F> void rejected(F f) { bool failed=false; try {f();} catch(const std::runtime_error&) {failed=true;} assert(failed); }
int main(int argc,char** argv) {
 assert(argc==7);
 auto s=read(argv[1]), m=read(argv[2]);
 auto b=read(argv[3]), l=read(argv[4]);
 auto ss=shapes(s,b,l), mm=maps(m);
 write(argv[5],ss);write(argv[6],mm);
 assert(get(mm,76,2)==36); auto expected=get(mm,68,4);put(mm,68,0,4);assert(crc(mm)==expected);
 assert(get(collection(ss,27),26,2)==8);
 rejected([&]{maps(Bytes(127));});
 auto broken=m;put(broken,72,0xffffffff,4);rejected([&]{maps(broken);});
 broken=m;auto start=get(m,get(m,72,4),4);put(broken,start+4,1,4);rejected([&]{maps(broken);});
 rejected([&]{shapes(s,b,Bytes(100));});
 rejected([&]{shapes(b,b,l);});
 rejected([&]{texture(0x1f00);});
 rejected([&]{texture(0x0528);});
 std::cout<<"PASS: 36 maps, eight landscapes, CRC, malformed-file rejection\n";
}

#pragma once
#include <algorithm>
#include <cmath>
namespace ac::viewer {
struct Size { int width, height; };
// Fit the actual video pixels inside the available client area, without upscaling.
inline Size fit(int width,int height,int available_width,int available_height,int long_edge=0) {
 if(width<1||height<1)return {320,320};
 double scale=std::min({1.,double(std::max(1,available_width))/width,double(std::max(1,available_height))/height});
 if(long_edge>0)scale=std::min(scale,double(long_edge)/std::max(width,height));
 return {std::max(1,int(std::lround(width*scale))),std::max(1,int(std::lround(height*scale)))};
}
}

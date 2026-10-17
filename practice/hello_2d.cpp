#include <cstdio>
#include "core/color.h"
#include "raster/line.h"
#include "core/framebuffer.h"
#include "core/image_write.h"

int main()
{
    Framebuffer fb(2048, 1152, Color(255, 255, 255));
    
    // 上半部分：普通线
    drawLine(fb, 0, 100, 2047, 400, Color(255, 255, 0));
    
    // 中间：填充圆角矩形
    fillRoundedRect(fb, 400, 450, 1200, 300, 50, Color(0, 128, 255));
    
    // 下半部分：抗锯齿线
    drawLineAA(fb, 0, 700, 2047, 1000, Color(0, 255, 0));
    
    imagewriter::writeImage("output/hello_2d.png", fb);
    
    return 0;
}

#include "llclipboardimage.h"
#include <cassert>
using namespace ll_clipboard_image;
namespace
{
void put(std::vector<unsigned char>& data, std::size_t at, std::uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) data[at + i] = static_cast<unsigned char>(value >> (8 * i)); }
std::vector<unsigned char> bitmap(unsigned header, unsigned bits, std::uint32_t height)
{
    std::vector<unsigned char> data(header + 16, 0); put(data, 0, header); put(data, 4, 2); put(data, 8, height); data[12] = 1; data[14] = static_cast<unsigned char>(bits); return data;
}
}
int main()
{
    auto data = bitmap(40, 24, 2); Dib info; assert(inspect(data, info) && info.stride == 8 && info.offset == 40);
    data[40] = 255; data[42] = 0; data[48] = 0; data[50] = 255;
    std::vector<unsigned char> rgba(16);
    assert(unpack(data, rgba.data(), rgba.size()) && rgba[2] == 255 && rgba[3] == 255 && rgba[8] == 255);
    put(data, 8, 0xfffffffeu); assert(unpack(data, rgba.data(), rgba.size()) && rgba[0] == 255 && rgba[10] == 255);
    auto alpha = bitmap(124, 32, 2); put(alpha, 16, 3); put(alpha, 40, 0xff0000); put(alpha, 44, 0xff00); put(alpha, 48, 0xff); put(alpha, 52, 0xff000000u);
    alpha[124 + 2] = 255; alpha[124 + 3] = 128;
    assert(unpack(alpha, rgba.data(), rgba.size()) && rgba[0] == 255 && rgba[3] == 128);
    auto opaque = bitmap(40, 32, 2); assert(unpack(opaque, rgba.data(), rgba.size()) && rgba[3] == 255);
    assert(!unpack(data, rgba.data(), 15) && !unpack(data, nullptr, 16));
    assert(!inspect({}, info));
    for (const auto at : {0u, 4u, 8u, 16u, 32u})
    { auto broken = data; put(broken, at, 0x80000000u); assert(!inspect(broken, info)); }
    auto broken = data; broken.resize(40); assert(!inspect(broken, info));
    broken = data; broken[14] = 8; assert(!inspect(broken, info));
    broken = alpha; put(broken, 40, 0x0000ff00); assert(!inspect(broken, info));
    broken = alpha; put(broken, 52, 0x0000000f); assert(!inspect(broken, info));
    broken = data; put(broken, 4, 16384); put(broken, 8, 16384); assert(!inspect(broken, info));
}

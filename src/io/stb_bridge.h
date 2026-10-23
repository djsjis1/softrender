#pragma once

// ============================================================
// stb 的 C 接口声明（私有）
//
// stb_image / stb_image_write 的实现宏集中在 third/tinygltf/tiny_gltf_impl.cpp，
// 编入 third_party_impl 静态库。公共 io 头不能出现任何 stb 符号，
// 因此声明集中到本私有头，只被 src/io/ 下的实现单元包含。
// ============================================================

extern "C"
{
unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels);
void stbi_image_free(void *retval_from_stbi_load);
void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);

int stbi_write_png(const char *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
int stbi_write_bmp(const char *filename, int w, int h, int comp, const void *data);
int stbi_write_tga(const char *filename, int w, int h, int comp, const void *data);
int stbi_write_jpg(const char *filename, int w, int h, int comp, const void *data, int quality);
int stbi_write_hdr(const char *filename, int w, int h, int comp, const float *data);
}

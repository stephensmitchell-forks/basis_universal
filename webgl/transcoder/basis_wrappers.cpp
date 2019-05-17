// basis_wrappers.cpp - Simple C-style wrappers to the C++ transcoder for WebGL use.
#include "basisu_transcoder.h"
#include <emscripten/bind.h>

#include <iostream>

using namespace emscripten;
using namespace basist;

static basist::etc1_global_selector_codebook *g_pGlobal_codebook;

void basis_init()
{
  basisu_transcoder_init();

  if (!g_pGlobal_codebook)
    g_pGlobal_codebook = new basist::etc1_global_selector_codebook(g_global_selector_cb_size, g_global_selector_cb);
}

#define MAGIC 0xDEADBEE1

struct basis_file
{
  int m_magic = 0;
	basisu_transcoder m_transcoder;
  std::vector<uint8_t> m_file;

  basis_file(const emscripten::val& jsBuffer)
    : m_file([&]() {
        size_t byteLength = jsBuffer["byteLength"].as<size_t>();
        return std::vector<uint8_t>(byteLength);
      }()),
      m_transcoder(g_pGlobal_codebook)
  {
    unsigned int length = jsBuffer["length"].as<unsigned int>();
    emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
    emscripten::val memoryView = jsBuffer["constructor"].new_(memory, reinterpret_cast<uintptr_t>(m_file.data()), length);
    memoryView.call<void>("set", jsBuffer);

    if (!m_transcoder.validate_header(m_file.data(), m_file.size())) {
      m_file.clear();
      std::cerr << "Invalid Basis header" << std::endl;
    }

    // Initialized after validation
    m_magic = MAGIC;
  }

  void close() {
    assert(m_magic == MAGIC);
    m_file.clear();
  }

  uint32_t getHasAlpha() {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    basisu_image_level_info li;
    if (!m_transcoder.get_image_level_info(m_file.data(), m_file.size(), li, 0, 0))
      return 0;

    return li.m_alpha_flag;
  }

  uint32_t getNumImages() {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    return m_transcoder.get_total_images(m_file.data(), m_file.size());
  }

  uint32_t getNumLevels(uint32_t image_index) {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    basisu_image_info ii;
    if (!m_transcoder.get_image_info(m_file.data(), m_file.size(), ii, image_index))
      return 0;

    return ii.m_total_levels;
  }

  uint32_t getImageWidth(uint32_t image_index, uint32_t level_index) {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    uint32_t orig_width, orig_height, total_blocks;
    if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_blocks))
      return 0;

    return orig_width;
  }

  uint32_t getImageHeight(uint32_t image_index, uint32_t level_index) {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    uint32_t orig_width, orig_height, total_blocks;
    if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_blocks))
      return 0;

    return orig_height;
  }

  uint32_t getImageTranscodedSizeInBytes(uint32_t image_index, uint32_t level_index, uint32_t format) {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    if (format >= cTFTotalTextureFormats)
      return 0;

    uint32_t bytes_per_block = basis_get_bytes_per_block(static_cast<transcoder_texture_format>(format));

    uint32_t orig_width, orig_height, total_blocks;
    if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_blocks))
      return 0;

    return total_blocks * bytes_per_block;
  }

  uint32_t startTranscoding() {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    return m_transcoder.start_transcoding(m_file.data(), m_file.size());
  }

  uint32_t transcodeImage(const emscripten::val& dst, uint32_t image_index, uint32_t level_index, uint32_t format, uint32_t pvrtc_wrap_addressing, uint32_t get_alpha_for_opaque_formats) {
    assert(m_magic == MAGIC);
    if (m_magic != MAGIC)
      return 0;

    if (format >= cTFTotalTextureFormats)
      return 0;

    uint32_t bytes_per_block = basis_get_bytes_per_block(static_cast<transcoder_texture_format>(format));

    uint32_t orig_width, orig_height, total_blocks;
    if (!m_transcoder.get_image_level_desc(m_file.data(), m_file.size(), image_index, level_index, orig_width, orig_height, total_blocks))
      return 0;

    uint32_t required_size = total_blocks * bytes_per_block;

    std::vector<uint8_t> dst_data(required_size);

    uint32_t status = m_transcoder.transcode_image_level(
      m_file.data(), m_file.size(), image_index, level_index,
      dst_data.data(), dst_data.size() / bytes_per_block,
      static_cast<basist::transcoder_texture_format>(format),
      (
        (pvrtc_wrap_addressing ? basisu_transcoder::cDecodeFlagsPVRTCWrapAddressing : 0) |
        (get_alpha_for_opaque_formats ? basisu_transcoder::cDecodeFlagsTranscodeAlphaDataToOpaqueFormats : 0)
      ));

    emscripten::val memory = emscripten::val::module_property("HEAP8")["buffer"];
    emscripten::val memoryView = emscripten::val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(dst_data.data()), dst_data.size());

    dst.call<void>("set", memoryView);
    return status;
  }
};

EMSCRIPTEN_BINDINGS(basis_transcoder) {

  function("initializeBasis", &basis_init);

  class_<basis_file>("BasisFile")
    .constructor<const emscripten::val&>()
    .function("close", optional_override([](basis_file& self) {
      return self.close();
    }))
    .function("getHasAlpha", optional_override([](basis_file& self) {
      return self.getHasAlpha();
    }))
    .function("getNumImages", optional_override([](basis_file& self) {
      return self.getNumImages();
    }))
    .function("getNumLevels", optional_override([](basis_file& self, uint32_t imageIndex) {
      return self.getNumLevels(imageIndex);
    }))
    .function("getImageWidth", optional_override([](basis_file& self, uint32_t imageIndex, uint32_t levelIndex) {
      return self.getImageWidth(imageIndex, levelIndex);
    }))
    .function("getImageHeight", optional_override([](basis_file& self, uint32_t imageIndex, uint32_t levelIndex) {
      return self.getImageHeight(imageIndex, levelIndex);
    }))
    .function("getImageTranscodedSizeInBytes", optional_override([](basis_file& self, uint32_t imageIndex, uint32_t levelIndex, uint32_t format) {
      return self.getImageTranscodedSizeInBytes(imageIndex, levelIndex, format);
    }))
    .function("startTranscoding", optional_override([](basis_file& self) {
      return self.startTranscoding();
    }))
    .function("transcodeImage", optional_override([](basis_file& self, const emscripten::val& dst, uint32_t imageIndex, uint32_t levelIndex, uint32_t format, uint32_t pvrtcWrapAddressing, uint32_t getAlphaForOpaqueFormats) {
      return self.transcodeImage(dst, imageIndex, levelIndex, format, pvrtcWrapAddressing, getAlphaForOpaqueFormats);
    }))
  ;

}

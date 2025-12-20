// This file contains the implementation of tiling operations.
// Do not include this file directly - it is included by tiling.hpp

#pragma once

#include <cassert>
#include <cstring>
#include <span>
#include <vector>
#include "../image_shape.hpp"
#include "../result.hpp"
#include "../types.hpp"

#ifndef TIFFCONCEPT_TILING_HEADER
#include "../tiling.hpp" // for linters
#endif

namespace tiffconcept {

template <ImageLayoutSpec OutSpec, ImageLayoutSpec InSpec, typename PixelType>
inline void copy_tile_to_tile(
    std::span<PixelType> dst_tile_data,
    const std::span<const PixelType> src_tile_data,
    const TileDimensions& dst_dims,
    const TileDimensions& src_dims,
    std::size_t copy_depth,
    std::size_t copy_height,
    std::size_t copy_width,
    std::size_t copy_nchans) noexcept
{
    assert (copy_depth + dst_dims.tile_z <= dst_dims.tile_depth);
    assert (copy_height + dst_dims.tile_y <= dst_dims.tile_height);
    assert (copy_width + dst_dims.tile_x <= dst_dims.tile_width);
    assert (copy_nchans + dst_dims.tile_s <= dst_dims.tile_nsamples);
    assert (copy_depth + src_dims.tile_z <= src_dims.tile_depth);
    assert (copy_height + src_dims.tile_y <= src_dims.tile_height);
    assert (copy_width + src_dims.tile_x <= src_dims.tile_width);
    assert (copy_nchans + src_dims.tile_s <= src_dims.tile_nsamples);

    if constexpr (InSpec == ImageLayoutSpec::DHWC && OutSpec == ImageLayoutSpec::DHWC) {
        // Both source and destination are DHWC
        const std::size_t src_hwc_slice_size = src_dims.tile_height * src_dims.tile_width * src_dims.tile_nsamples;
        const std::size_t dst_hwc_slice_size = dst_dims.tile_height * dst_dims.tile_width * dst_dims.tile_nsamples;
        const std::size_t src_wc_slice_size = src_dims.tile_width * src_dims.tile_nsamples;
        const std::size_t dst_wc_slice_size = dst_dims.tile_width * dst_dims.tile_nsamples;
        const std::size_t src_c_slice_size = src_dims.tile_nsamples;
        const std::size_t dst_c_slice_size = dst_dims.tile_nsamples;

        const std::size_t src_start_index = src_dims.tile_s +
            src_dims.tile_x * src_c_slice_size +
            src_dims.tile_y * src_wc_slice_size +
            src_dims.tile_z * src_hwc_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_s +
            dst_dims.tile_x * dst_c_slice_size +
            dst_dims.tile_y * dst_wc_slice_size +
            dst_dims.tile_z * dst_hwc_slice_size;

        // HWC contiguous copy
        if (copy_nchans == src_dims.tile_nsamples && copy_nchans == dst_dims.tile_nsamples) {
            if (copy_width == src_dims.tile_width && copy_width == dst_dims.tile_width) {
                // hWC copy
                for (std::size_t d = 0; d < copy_depth; ++d) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + d * dst_hwc_slice_size],
                        &src_tile_data[src_start_index + d * src_hwc_slice_size],
                        copy_height * copy_width * copy_nchans * sizeof(PixelType)
                    );
                }
                return;
            }
        }

        // WC contiguous copy
        if (copy_nchans == src_dims.tile_nsamples && copy_nchans == dst_dims.tile_nsamples) {
            // wC  copy
            for (std::size_t d = 0; d < copy_depth; ++d) {
                for (std::size_t h = 0; h < copy_height; ++h) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + d * dst_hwc_slice_size + h * dst_wc_slice_size],
                        &src_tile_data[src_start_index + d * src_hwc_slice_size + h * src_wc_slice_size],
                        copy_width * copy_nchans * sizeof(PixelType)
                    );
                }
            }
            return;
        }

        // Generic fallback
        for (std::size_t d = 0; d < copy_depth; ++d) {
            for (std::size_t h = 0; h < copy_height; ++h) {
                for (std::size_t w = 0; w < copy_width; ++w) {
                    for (std::size_t c = 0; c < copy_nchans; ++c) {
                        dst_tile_data[dst_start_index +
                            d * dst_hwc_slice_size +
                            h * dst_wc_slice_size +
                            w * dst_c_slice_size +
                            c] =
                        src_tile_data[src_start_index +
                            d * src_hwc_slice_size +
                            h * src_wc_slice_size +
                            w * src_c_slice_size +
                            c];
                    }
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::CDHW && OutSpec == ImageLayoutSpec::CDHW) {
        // Both source and destination are CDHW
        const std::size_t src_dhw_slice_size = src_dims.tile_depth * src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_dhw_slice_size = dst_dims.tile_depth * dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_hw_slice_size = src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_hw_slice_size = dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_w_slice_size = src_dims.tile_width;
        const std::size_t dst_w_slice_size = dst_dims.tile_width;

        const std::size_t src_start_index = src_dims.tile_x +
            src_dims.tile_y * src_w_slice_size +
            src_dims.tile_z * src_hw_slice_size +
            src_dims.tile_s * src_dhw_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_x +
            dst_dims.tile_y * dst_w_slice_size +
            dst_dims.tile_z * dst_hw_slice_size +
            dst_dims.tile_s * dst_dhw_slice_size;

        // DHW contiguous copy
        if (copy_height == src_dims.tile_height && copy_height == dst_dims.tile_height) {
            if (copy_width == src_dims.tile_width && copy_width == dst_dims.tile_width) {
                // dHW copy
                for (std::size_t c = 0; c < copy_nchans; ++c) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + c * dst_dhw_slice_size],
                        &src_tile_data[src_start_index + c * src_dhw_slice_size],
                        copy_depth * copy_height * copy_width * sizeof(PixelType)
                    );
                }
                return;
            }
        }

        // HW contiguous copy
        if (copy_width == src_dims.tile_width && copy_width == dst_dims.tile_width) {
            // hW copy
            for (std::size_t c = 0; c < copy_nchans; ++c) {
                for (std::size_t d = 0; d < copy_depth; ++d) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + c * dst_dhw_slice_size + d * dst_hw_slice_size],
                        &src_tile_data[src_start_index + c * src_dhw_slice_size + d * src_hw_slice_size],
                        copy_height * copy_width * sizeof(PixelType)
                    );
                }
            }
            return;
        }
        // Generic fallback
        for (std::size_t c = 0; c < copy_nchans; ++c) {
            for (std::size_t d = 0; d < copy_depth; ++d) {
                for (std::size_t h = 0; h < copy_height; ++h) {
                    for (std::size_t w = 0; w < copy_width; ++w) {
                        dst_tile_data[dst_start_index +
                            c * dst_dhw_slice_size +
                            d * dst_hw_slice_size +
                            h * dst_w_slice_size +
                            w] =
                        src_tile_data[src_start_index +
                            c * src_dhw_slice_size +
                            d * src_hw_slice_size +
                            h * src_w_slice_size +
                            w];
                    }
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::DCHW && OutSpec == ImageLayoutSpec::DCHW) {
        // Both source and destination are DCHW
        const std::size_t src_chw_slice_size = src_dims.tile_nsamples * src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_chw_slice_size = dst_dims.tile_nsamples * dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_hw_slice_size = src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_hw_slice_size = dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_w_slice_size = src_dims.tile_width;
        const std::size_t dst_w_slice_size = dst_dims.tile_width;

        const std::size_t src_start_index = src_dims.tile_x +
            src_dims.tile_y * src_w_slice_size +
            src_dims.tile_s * src_hw_slice_size +
            src_dims.tile_z * src_chw_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_x +
            dst_dims.tile_y * dst_w_slice_size +
            dst_dims.tile_s * dst_hw_slice_size +
            dst_dims.tile_z * dst_chw_slice_size;

        // CHW contiguous copy
        if (copy_height == src_dims.tile_height && copy_height == dst_dims.tile_height) {
            if (copy_width == src_dims.tile_width && copy_width == dst_dims.tile_width) {
                // cHW copy
                for (std::size_t d = 0; d < copy_depth; ++d) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + d * dst_chw_slice_size],
                        &src_tile_data[src_start_index + d * src_chw_slice_size],
                        copy_nchans * copy_height * copy_width * sizeof(PixelType)
                    );
                }
                return;
            }
        }

        // HW contiguous copy
        if (copy_width == src_dims.tile_width && copy_width == dst_dims.tile_width) {
            // hW copy
            for (std::size_t d = 0; d < copy_depth; ++d) {
                for (std::size_t c = 0; c < copy_nchans; ++c) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + d * dst_chw_slice_size + c * dst_hw_slice_size],
                        &src_tile_data[src_start_index + d * src_chw_slice_size + c * src_hw_slice_size],
                        copy_height * copy_width * sizeof(PixelType)
                    );
                }
            }
            return;
        }

        // Generic fallback
        for (std::size_t d = 0; d < copy_depth; ++d) {
            for (std::size_t c = 0; c < copy_nchans; ++c) {
                for (std::size_t h = 0; h < copy_height; ++h) {
                    for (std::size_t w = 0; w < copy_width; ++w) {
                        dst_tile_data[dst_start_index +
                            d * dst_chw_slice_size +
                            c * dst_hw_slice_size +
                            h * dst_w_slice_size +
                            w] =
                        src_tile_data[src_start_index +
                            d * src_chw_slice_size +
                            c * src_hw_slice_size +
                            h * src_w_slice_size +
                            w];
                    }
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::CDHW && OutSpec == ImageLayoutSpec::DHWC) {
        // Source is CDHW, destination is DHWC
        const std::size_t src_dhw_slice_size = src_dims.tile_depth * src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_hwc_slice_size = dst_dims.tile_height * dst_dims.tile_width * dst_dims.tile_nsamples;
        const std::size_t src_hw_slice_size = src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_wc_slice_size = dst_dims.tile_width * dst_dims.tile_nsamples;
        const std::size_t src_w_slice_size = src_dims.tile_width;
        const std::size_t dst_c_slice_size = dst_dims.tile_nsamples;

        const std::size_t src_start_index = src_dims.tile_x +
            src_dims.tile_y * src_w_slice_size +
            src_dims.tile_z * src_hw_slice_size +
            src_dims.tile_s * src_dhw_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_s +
            dst_dims.tile_x * dst_c_slice_size +
            dst_dims.tile_y * dst_wc_slice_size +
            dst_dims.tile_z * dst_hwc_slice_size;

        if (copy_nchans == 1 && dst_dims.tile_nsamples == 1 && dst_dims.tile_s == 0) {
            // DHW: DHW
            copy_tile_to_tile<ImageLayoutSpec::CDHW, ImageLayoutSpec::CDHW, PixelType>(
                dst_tile_data, src_tile_data,
                dst_dims, src_dims,
                copy_depth, copy_height, copy_width, 1
            );
            return;
        }

        // Generic fallback (it is possible to do better)
        for (std::size_t d = 0; d < copy_depth; ++d) {
            for (std::size_t h = 0; h < copy_height; ++h) {
                for (std::size_t w = 0; w < copy_width; ++w) {
                    for (std::size_t c = 0; c < copy_nchans; ++c) {
                        dst_tile_data[dst_start_index +
                            d * dst_hwc_slice_size +
                            h * dst_wc_slice_size +
                            w * dst_c_slice_size +
                            c] =
                        src_tile_data[src_start_index +
                            c * src_dhw_slice_size +
                            d * src_hw_slice_size +
                            h * src_w_slice_size +
                            w];
                    }
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::DHWC && OutSpec == ImageLayoutSpec::CDHW) {
        // Source is DHWC, destination is CDHW
        const std::size_t src_hwc_slice_size = src_dims.tile_height * src_dims.tile_width * src_dims.tile_nsamples;
        const std::size_t dst_dhw_slice_size = dst_dims.tile_depth * dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_wc_slice_size = src_dims.tile_width * src_dims.tile_nsamples;
        const std::size_t dst_hw_slice_size = dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_c_slice_size = src_dims.tile_nsamples;
        const std::size_t dst_w_slice_size = dst_dims.tile_width;

        const std::size_t src_start_index = src_dims.tile_s +
            src_dims.tile_x * src_c_slice_size +
            src_dims.tile_y * src_wc_slice_size +
            src_dims.tile_z * src_hwc_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_x +
            dst_dims.tile_y * dst_w_slice_size +
            dst_dims.tile_z * dst_hw_slice_size +
            dst_dims.tile_s * dst_dhw_slice_size;

        if (copy_nchans == 1 && src_dims.tile_nsamples == 1 && src_dims.tile_s == 0) {
            // DHW: DHW
            copy_tile_to_tile<ImageLayoutSpec::CDHW, ImageLayoutSpec::CDHW, PixelType>(
                dst_tile_data, src_tile_data,
                dst_dims, src_dims,
                copy_depth, copy_height, copy_width, 1
            );
            return;
        }

        // Generic fallback (it is possible to do better)
        for (std::size_t c = 0; c < copy_nchans; ++c) {
            for (std::size_t d = 0; d < copy_depth; ++d) {
                for (std::size_t h = 0; h < copy_height; ++h) {
                    for (std::size_t w = 0; w < copy_width; ++w) {
                        dst_tile_data[dst_start_index +
                            c * dst_dhw_slice_size +
                            d * dst_hw_slice_size +
                            h * dst_w_slice_size +
                            w] =
                        src_tile_data[src_start_index +
                            d * src_hwc_slice_size +
                            h * src_wc_slice_size +
                            w * src_c_slice_size +
                            c];
                    }
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::CDHW && OutSpec == ImageLayoutSpec::DCHW) {
        // Source is CDHW, destination is DCHW
        const std::size_t src_dhw_slice_size = src_dims.tile_depth * src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_chw_slice_size = dst_dims.tile_nsamples * dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_hw_slice_size = src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_hw_slice_size = dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_w_slice_size = src_dims.tile_width;
        const std::size_t dst_w_slice_size = dst_dims.tile_width;

        const std::size_t src_start_index = src_dims.tile_x +
            src_dims.tile_y * src_w_slice_size +
            src_dims.tile_z * src_hw_slice_size +
            src_dims.tile_s * src_dhw_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_x +
            dst_dims.tile_y * dst_w_slice_size +
            dst_dims.tile_s * dst_hw_slice_size +
            dst_dims.tile_z * dst_chw_slice_size;

        // HW contiguous copy
        if (copy_width == src_dims.tile_width && copy_width == dst_dims.tile_width) {
            // hW copy
            for (std::size_t c = 0; c < copy_nchans; ++c) {
                for (std::size_t d = 0; d < copy_depth; ++d) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + d * dst_chw_slice_size + c * dst_hw_slice_size],
                        &src_tile_data[src_start_index + c * src_dhw_slice_size + d * src_hw_slice_size],
                        copy_height * copy_width * sizeof(PixelType)
                    );
                }
            }
            return;
        }

        // Generic fallback
        for (std::size_t c = 0; c < copy_nchans; ++c) {
            for (std::size_t d = 0; d < copy_depth; ++d) {
                for (std::size_t h = 0; h < copy_height; ++h) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + d * dst_chw_slice_size + c * dst_hw_slice_size + h * dst_w_slice_size],
                        &src_tile_data[src_start_index + c * src_dhw_slice_size + d * src_hw_slice_size + h * src_w_slice_size],
                        copy_width * sizeof(PixelType)
                    );
                    // Note: the memcpy path could also be used when the inner for loop is the channels,
                    // however it is much more likely to have large width than large number of channels.
                    // and I am afraid of memcpy overhead when called many times with mini sizes.
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::DCHW && OutSpec == ImageLayoutSpec::CDHW) {
        // Source is DCHW, destination is CDHW
        const std::size_t src_chw_slice_size = src_dims.tile_nsamples * src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_dhw_slice_size = dst_dims.tile_depth * dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_hw_slice_size = src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_hw_slice_size = dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_w_slice_size = src_dims.tile_width;
        const std::size_t dst_w_slice_size = dst_dims.tile_width;

        const std::size_t src_start_index = src_dims.tile_s * src_chw_slice_size +
            src_dims.tile_z * src_hw_slice_size +
            src_dims.tile_y * src_w_slice_size +
            src_dims.tile_x;
        const std::size_t dst_start_index = dst_dims.tile_x +
            dst_dims.tile_y * dst_w_slice_size +
            dst_dims.tile_z * dst_hw_slice_size +
            dst_dims.tile_s * dst_dhw_slice_size;

        // HW contiguous copy
        if (copy_width == src_dims.tile_width && copy_width == dst_dims.tile_width) {
            // hW copy
            for (std::size_t c = 0; c < copy_nchans; ++c) {
                for (std::size_t d = 0; d < copy_depth; ++d) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + c * dst_dhw_slice_size + d * dst_hw_slice_size],
                        &src_tile_data[src_start_index + d * src_chw_slice_size + c * src_hw_slice_size],
                        copy_height * copy_width * sizeof(PixelType)
                    );
                }
            }
            return;
        }

        // Generic fallback
        for (std::size_t c = 0; c < copy_nchans; ++c) {
            for (std::size_t d = 0; d < copy_depth; ++d) {
                for (std::size_t h = 0; h < copy_height; ++h) {
                    std::memcpy(
                        &dst_tile_data[dst_start_index + c * dst_dhw_slice_size + d * dst_hw_slice_size + h * dst_w_slice_size],
                        &src_tile_data[src_start_index + d * src_chw_slice_size + c * src_hw_slice_size + h * src_w_slice_size],
                        copy_width * sizeof(PixelType)
                    );
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::DHWC && OutSpec == ImageLayoutSpec::DCHW) {
        // Source is DHWC, destination is DCHW
        const std::size_t src_hwc_slice_size = src_dims.tile_height * src_dims.tile_width * src_dims.tile_nsamples;
        const std::size_t dst_chw_slice_size = dst_dims.tile_nsamples * dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_wc_slice_size = src_dims.tile_width * src_dims.tile_nsamples;
        const std::size_t dst_hw_slice_size = dst_dims.tile_height * dst_dims.tile_width;
        const std::size_t src_c_slice_size = src_dims.tile_nsamples;
        const std::size_t dst_w_slice_size = dst_dims.tile_width;

        const std::size_t src_start_index = src_dims.tile_s +
            src_dims.tile_x * src_c_slice_size +
            src_dims.tile_y * src_wc_slice_size +
            src_dims.tile_z * src_hwc_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_x +
            dst_dims.tile_y * dst_w_slice_size +
            dst_dims.tile_s * dst_hw_slice_size +
            dst_dims.tile_z * dst_chw_slice_size;

        if (copy_nchans == 1 && src_dims.tile_nsamples == 1 && src_dims.tile_s == 0) {
            // DCHW: DCHW
            copy_tile_to_tile<ImageLayoutSpec::DCHW, ImageLayoutSpec::DCHW, PixelType>(
                dst_tile_data, src_tile_data,
                dst_dims, src_dims,
                copy_depth, copy_height, copy_width, 1
            );
            return;
        }

        // Generic fallback (it is possible to do better)
        for (std::size_t c = 0; c < copy_nchans; ++c) {
            for (std::size_t d = 0; d < copy_depth; ++d) {
                for (std::size_t h = 0; h < copy_height; ++h) {
                    for (std::size_t w = 0; w < copy_width; ++w) {
                        dst_tile_data[dst_start_index +
                            d * dst_chw_slice_size +
                            c * dst_hw_slice_size +
                            h * dst_w_slice_size +
                            w] =
                        src_tile_data[src_start_index +
                            d * src_hwc_slice_size +
                            h * src_wc_slice_size +
                            w * src_c_slice_size +
                            c];
                    }
                }
            }
        }
    }
    else if constexpr (InSpec == ImageLayoutSpec::DCHW && OutSpec == ImageLayoutSpec::DHWC) {
        // Source is DCHW, destination is DHWC
        const std::size_t src_chw_slice_size = src_dims.tile_nsamples * src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_hwc_slice_size = dst_dims.tile_height * dst_dims.tile_width * dst_dims.tile_nsamples;
        const std::size_t src_hw_slice_size = src_dims.tile_height * src_dims.tile_width;
        const std::size_t dst_wc_slice_size = dst_dims.tile_width * dst_dims.tile_nsamples;
        const std::size_t src_w_slice_size = src_dims.tile_width;
        const std::size_t dst_c_slice_size = dst_dims.tile_nsamples;

        const std::size_t src_start_index = src_dims.tile_x +
            src_dims.tile_y * src_w_slice_size +
            src_dims.tile_s * src_hw_slice_size +
            src_dims.tile_z * src_chw_slice_size;
        const std::size_t dst_start_index = dst_dims.tile_s +
            dst_dims.tile_x * dst_c_slice_size +
            dst_dims.tile_y * dst_wc_slice_size +
            dst_dims.tile_z * dst_hwc_slice_size;

        if (copy_nchans == 1 && dst_dims.tile_nsamples == 1 && dst_dims.tile_s == 0) {
            // DCHW: DCHW
            copy_tile_to_tile<ImageLayoutSpec::DCHW, ImageLayoutSpec::DCHW, PixelType>(
                dst_tile_data, src_tile_data,
                dst_dims, src_dims,
                copy_depth, copy_height, copy_width, 1
            );
            return;
        }

        // Generic fallback
        for (std::size_t d = 0; d < copy_depth; ++d) {
            for (std::size_t h = 0; h < copy_height; ++h) {
                for (std::size_t w = 0; w < copy_width; ++w) {
                    for (std::size_t c = 0; c < copy_nchans; ++c) {
                        dst_tile_data[dst_start_index +
                            d * dst_hwc_slice_size +
                            h * dst_wc_slice_size +
                            w * dst_c_slice_size +
                            c] =
                        src_tile_data[src_start_index +
                            d * src_chw_slice_size +
                            c * src_hw_slice_size +
                            h * src_w_slice_size +
                            w];
                    }
                }
            }
        }
    }
    else {
        static_assert(false, "Unsupported combination of input and output specifications");
    }
}

/// Copy a tile (or a portion of a tile) into a larger image buffer
/// Template parameters control memory layout and enable compile-time optimization
/// @tparam PlanarConfig Whether tile data is chunky (interleaved) or planar (separate channels)
/// @tparam OutSpec Output buffer layout (DHWC, DCHW, or CDHW)
template <PlanarConfiguration PlanarConfig, ImageLayoutSpec OutSpec, typename PixelType>
inline void copy_tile_to_buffer(
    std::span<const PixelType> tile_data,
    std::span<PixelType> output_buffer,
    uint32_t tile_depth,
    uint32_t tile_height,
    uint32_t tile_width,
    uint16_t tile_nsamples,         // Number of samples (channels) in tile
    uint32_t tile_start_depth,      // Relative start depth in tile
    uint32_t tile_start_height,
    uint32_t tile_start_width,
    uint16_t tile_start_sample,     // Starting sample/channel in tile
    uint32_t output_depth,
    uint32_t output_height,
    uint32_t output_width,
    uint16_t output_nchans,         // Total channels in output buffer
    uint32_t output_start_depth,
    uint32_t output_start_height,
    uint32_t output_start_width,
    uint16_t output_start_chan,
    uint32_t copy_depth,
    uint32_t copy_height,
    uint32_t copy_width,
    uint16_t copy_nchans) noexcept
{
    // copy_tile_to_buffer inputs are sanitized by the caller
    // such that the tile to copy fits within both the tile and output buffer.
    assert (copy_depth + output_start_depth <= output_depth);
    assert (copy_height + output_start_height <= output_height);
    assert (copy_width + output_start_width <= output_width);
    assert (copy_nchans + output_start_chan <= output_nchans);
    assert (copy_depth + tile_start_depth <= tile_depth);
    assert (copy_height + tile_start_height <= tile_height);
    assert (copy_width + tile_start_width <= tile_width);
    assert (copy_nchans + tile_start_sample <= tile_nsamples);
    assert (copy_depth > 0);
    assert (copy_height > 0);
    assert (copy_width > 0);
    assert (copy_nchans > 0);

    if constexpr (PlanarConfig == PlanarConfiguration::Planar) {
        assert (tile_nsamples == 1); // Planar tiles must have a single sample per tile
    }

    TileDimensions tile_dims{
        static_cast<std::size_t>(tile_depth),
        static_cast<std::size_t>(tile_height),
        static_cast<std::size_t>(tile_width),
        static_cast<std::size_t>(tile_nsamples),
        static_cast<std::size_t>(tile_start_depth),
        static_cast<std::size_t>(tile_start_height),
        static_cast<std::size_t>(tile_start_width),
        static_cast<std::size_t>(tile_start_sample)
    };

    TileDimensions output_dims{
        static_cast<std::size_t>(output_depth),
        static_cast<std::size_t>(output_height),
        static_cast<std::size_t>(output_width),
        static_cast<std::size_t>(output_nchans),
        static_cast<std::size_t>(output_start_depth),
        static_cast<std::size_t>(output_start_height),
        static_cast<std::size_t>(output_start_width),
        static_cast<std::size_t>(output_start_chan)
    };

    // Chunky is equivalent to DHWC layout
    if constexpr (PlanarConfig == PlanarConfiguration::Chunky) {
        copy_tile_to_tile<OutSpec, ImageLayoutSpec::DHWC, PixelType>(
            output_buffer, tile_data,
            output_dims, tile_dims,
            copy_depth, copy_height, copy_width, copy_nchans
        );
    }
    else if constexpr (PlanarConfig == PlanarConfiguration::Planar) {
        // Planar tiles are treated as CDHW with a single channel per tile
        copy_tile_to_tile<OutSpec, ImageLayoutSpec::CDHW, PixelType>(
            output_buffer, tile_data,
            output_dims, tile_dims,
            copy_depth, copy_height, copy_width, copy_nchans
        );
    } else {
        static_assert(false, "Unsupported PlanarConfiguration");
    }
}

/// Copy data from a larger image buffer into a tile (or a portion of a tile)
/// Handles tiles that extend beyond the input buffer boundaries by using replicate padding
/// Template parameters control memory layout and enable compile-time optimization
/// @tparam PlanarConfig Whether tile data is chunky (interleaved) or planar (separate channels)
/// @tparam InSpec Input buffer layout (DHWC, DCHW, or CDHW)
template <PlanarConfiguration PlanarConfig, ImageLayoutSpec InSpec, typename PixelType>
inline void fetch_tile_from_buffer(
    std::span<const PixelType> input_buffer,
    std::span<PixelType> tile_data,
    uint32_t input_depth,
    uint32_t input_height,
    uint32_t input_width,
    uint16_t input_nchans,         // Total channels in input buffer
    uint32_t input_start_depth,
    uint32_t input_start_height,
    uint32_t input_start_width,
    uint16_t input_start_chan,
    uint32_t tile_depth,
    uint32_t tile_height,
    uint32_t tile_width,
    uint16_t tile_nsamples) noexcept
{
    // Validate that the region to copy fits within the tile
    assert (tile_depth > 0);
    assert (tile_height > 0);
    assert (tile_width > 0);
    assert (tile_nsamples > 0);

    if constexpr (PlanarConfig == PlanarConfiguration::Planar) {
        assert (tile_nsamples == 1); // Planar tiles must have a single sample per tile
    }

    TileDimensions input_dims{
        static_cast<std::size_t>(input_depth),
        static_cast<std::size_t>(input_height),
        static_cast<std::size_t>(input_width),
        static_cast<std::size_t>(input_nchans),
        static_cast<std::size_t>(input_start_depth),
        static_cast<std::size_t>(input_start_height),
        static_cast<std::size_t>(input_start_width),
        static_cast<std::size_t>(input_start_chan)
    };

    TileDimensions tile_dims{
        static_cast<std::size_t>(tile_depth),
        static_cast<std::size_t>(tile_height),
        static_cast<std::size_t>(tile_width),
        static_cast<std::size_t>(tile_nsamples),
        static_cast<std::size_t>(0),
        static_cast<std::size_t>(0),
        static_cast<std::size_t>(0),
        static_cast<std::size_t>(0)
    };

    // Calculate the actual region we can copy from the input buffer
    const std::size_t actual_copy_depth = std::min(static_cast<std::size_t>(tile_depth), 
        input_dims.tile_depth > input_dims.tile_z ? input_dims.tile_depth - input_dims.tile_z : 0u);
    const std::size_t actual_copy_height = std::min(static_cast<std::size_t>(tile_height),
        input_dims.tile_height > input_dims.tile_y ? input_dims.tile_height - input_dims.tile_y : 0u);
    const std::size_t actual_copy_width = std::min(static_cast<std::size_t>(tile_width),
        input_dims.tile_width > input_dims.tile_x ? input_dims.tile_width - input_dims.tile_x : 0u);
    const std::size_t actual_copy_nchans = std::min(static_cast<std::size_t>(tile_nsamples),
        input_dims.tile_nsamples > input_dims.tile_s ? input_dims.tile_nsamples - input_dims.tile_s : 0u);

    if (actual_copy_depth == 0 || actual_copy_height == 0 || actual_copy_width == 0 || actual_copy_nchans == 0) {
        // Nothing to copy from input buffer, entire tile region will be padding
        // This shouldn't occur, but just in case, we handle it gracefully
        for (std::size_t i = 0; i < tile_depth * tile_height * tile_width * tile_nsamples; ++i) {
            tile_data[i] = PixelType{}; // Default initialize
        }
        return;
    }

    // Chunky is equivalent to DHWC layout
    if constexpr (PlanarConfig == PlanarConfiguration::Chunky) {
        copy_tile_to_tile<ImageLayoutSpec::DHWC, InSpec, PixelType>(
            tile_data, input_buffer,
            tile_dims, input_dims,
            actual_copy_depth, actual_copy_height, actual_copy_width, actual_copy_nchans
        );
    }
    else if constexpr (PlanarConfig == PlanarConfiguration::Planar) {
        // Planar tiles are treated as CDHW with a single channel per tile
        copy_tile_to_tile<ImageLayoutSpec::CDHW, InSpec, PixelType>(
            tile_data, input_buffer,
            tile_dims, input_dims,
            actual_copy_depth, actual_copy_height, actual_copy_width, actual_copy_nchans
        );
    }

    // Now handle padding with replicate border mode
    const bool needs_padding = (actual_copy_depth < tile_dims.tile_depth) || 
                               (actual_copy_height < tile_dims.tile_height) || 
                               (actual_copy_width < tile_dims.tile_width) ||
                               (actual_copy_nchans < tile_dims.tile_nsamples);
    
    if (!needs_padding) {
        return;
    }

    // Replicate padding: copy edge values to fill the padded regions
    // We need to handle this differently based on the tile layout
    if constexpr (PlanarConfig == PlanarConfiguration::Chunky) {
        // DHWC layout
        const std::size_t tile_hwc_slice_size = tile_dims.tile_height * tile_dims.tile_width * tile_dims.tile_nsamples;
        const std::size_t tile_wc_slice_size = tile_dims.tile_width * tile_dims.tile_nsamples;
        const std::size_t tile_c_slice_size = tile_dims.tile_nsamples;
        
        // Pad channels (if needed) - replicate last channel
        if (actual_copy_nchans < tile_dims.tile_nsamples) {
            for (std::size_t d = 0; d < actual_copy_depth; ++d) {
                for (std::size_t h = 0; h < actual_copy_height; ++h) {
                    for (std::size_t w = 0; w < actual_copy_width; ++w) {
                        const std::size_t src_idx = d * tile_hwc_slice_size +
                                                    h * tile_wc_slice_size +
                                                    w * tile_dims.tile_nsamples +
                                                    actual_copy_nchans - 1;
                        const PixelType edge_value = tile_data[src_idx];
                        
                        for (uint16_t c = actual_copy_nchans; c < tile_dims.tile_nsamples; ++c) {
                            const std::size_t dst_idx = d * tile_hwc_slice_size +
                                                        h * tile_wc_slice_size +
                                                        w * tile_dims.tile_nsamples +
                                                        c;
                            tile_data[dst_idx] = edge_value;
                        }
                    }
                }
            }
        }

        // Note: this padding replication might not be the best,
        // maybe it would make more sense at least for the farthest slices
        // to contain constant data rather than replicated data.
        
        // Pad width (replicate rightmost column)
        if (actual_copy_width < tile_dims.tile_width) {
            for (std::size_t d = 0; d < actual_copy_depth; ++d) {
                for (std::size_t h = 0; h < actual_copy_height; ++h) {
                    const std::size_t src_offset = d * tile_hwc_slice_size +
                                                   h * tile_wc_slice_size +
                                                   (actual_copy_width - 1) * tile_dims.tile_nsamples;
                    
                    for (std::size_t w = actual_copy_width; w < tile_dims.tile_width; ++w) {
                        const std::size_t dst_offset = d * tile_hwc_slice_size +
                                                       h * tile_wc_slice_size +
                                                       w * tile_dims.tile_nsamples;
                        std::memcpy(&tile_data[dst_offset], &tile_data[src_offset], tile_c_slice_size * sizeof(PixelType));
                    }
                }
            }
        }
        
        // Pad height (replicate bottom row)
        if (actual_copy_height < tile_dims.tile_height) {
            for (std::size_t d = 0; d < actual_copy_depth; ++d) {
                const std::size_t src_offset = d * tile_hwc_slice_size +
                                               (actual_copy_height - 1) * tile_wc_slice_size;
                
                for (uint32_t h = actual_copy_height; h < tile_dims.tile_height; ++h) {
                    const std::size_t dst_offset = d * tile_hwc_slice_size +
                                                   h * tile_wc_slice_size;
                    std::memcpy(&tile_data[dst_offset], &tile_data[src_offset], 
                                tile_wc_slice_size * sizeof(PixelType));
                }
            }
        }
        
        // Pad depth (replicate farthest slice)
        if (actual_copy_depth < tile_dims.tile_depth) {
            const std::size_t src_offset = (actual_copy_depth - 1) * tile_hwc_slice_size;
            
            for (uint32_t d = actual_copy_depth; d < tile_dims.tile_depth; ++d) {
                const std::size_t dst_offset = d * tile_hwc_slice_size;
                std::memcpy(&tile_data[dst_offset],
                            &tile_data[src_offset],
                            tile_hwc_slice_size * sizeof(PixelType));
            }
        }
    }
    else if constexpr (PlanarConfig == PlanarConfiguration::Planar) {
        // CDHW layout (planar - single channel per tile)
        const std::size_t tile_hw_slice_size = tile_dims.tile_height * tile_dims.tile_width;
        const std::size_t tile_w_slice_size = tile_dims.tile_width;
        
        // Pad width (replicate rightmost column)
        if (actual_copy_width < tile_dims.tile_width) {
            for (std::size_t d = 0; d < actual_copy_depth; ++d) {
                for (std::size_t h = 0; h < actual_copy_height; ++h) {
                    const std::size_t src_idx = d * tile_hw_slice_size +
                                                h * tile_w_slice_size +
                                                actual_copy_width - 1;
                    const PixelType edge_value = tile_data[src_idx];
                    
                    for (std::size_t w = actual_copy_width; w < tile_dims.tile_width; ++w) {
                        const std::size_t dst_idx = d * tile_hw_slice_size +
                                                    h * tile_w_slice_size +
                                                    w;
                        tile_data[dst_idx] = edge_value;
                    }
                }
            }
        }
        
        // Pad height (replicate bottom row)
        if (actual_copy_height < tile_dims.tile_height) {
            for (std::size_t d = 0; d < actual_copy_depth; ++d) {
                const std::size_t src_offset = d * tile_hw_slice_size +
                                               (actual_copy_height - 1) * tile_w_slice_size;
                
                for (std::size_t h = actual_copy_height; h < tile_dims.tile_height; ++h) {
                    const std::size_t dst_offset = d * tile_hw_slice_size +
                                                   h * tile_w_slice_size;
                    std::memcpy(&tile_data[dst_offset], &tile_data[src_offset], 
                                tile_w_slice_size * sizeof(PixelType));
                }
            }
        }
        
        // Pad depth (replicate farthest slice)
        if (actual_copy_depth < tile_dims.tile_depth) {
            const std::size_t src_offset = (actual_copy_depth - 1) * tile_hw_slice_size;
            
            for (std::size_t d = actual_copy_depth; d < tile_dims.tile_depth; ++d) {
                const std::size_t dst_offset = d * tile_hw_slice_size;
                std::memcpy(&tile_data[dst_offset],
                            &tile_data[src_offset],
                            tile_hw_slice_size * sizeof(PixelType));
            }
        }
    }  
}
// ============================================================================
// TiledImageInfo Member Function Implementations
// ============================================================================

template <typename PixelType>
TiledImageInfo<PixelType>::TiledImageInfo() noexcept
    : shape_()
    , tile_width_(0)
    , tile_height_(0)
    , tile_depth_(1)
    , tile_offsets_()
    , tile_byte_counts_()
    , compression_(CompressionScheme::None)
    , predictor_(Predictor::None)
    , tiles_across_(0)
    , tiles_down_(0)
    , tiles_deep_(1) {}

template <typename PixelType>
template <typename TagSpec>
    requires TiledImageTagSpec<TagSpec>
Result<void> TiledImageInfo<PixelType>::update_from_metadata(
    const ExtractedTags<TagSpec>& metadata) noexcept {
    
    // Extract common image shape first
    auto shape_result = shape_.update_from_metadata(metadata);
    if (shape_result.is_error()) [[unlikely]] {
        return shape_result;
    }

    // Validate pixel type
    auto format_validation = shape_.validate_pixel_type<PixelType>();
    if (format_validation.is_error()) [[unlikely]] {
        return format_validation;
    }
    
    // Extract tile-specific fields
    auto tile_width_val = metadata.template get<TagCode::TileWidth>();
    auto tile_length_val = metadata.template get<TagCode::TileLength>();
    auto tile_offsets_val = metadata.template get<TagCode::TileOffsets>();
    auto tile_byte_counts_val = metadata.template get<TagCode::TileByteCounts>();
    
    // Validation
    if (!optional::is_value_present(tile_width_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "TileWidth tag not found");
    }
    if (!optional::is_value_present(tile_length_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "TileLength tag not found");
    }
    if (!optional::is_value_present(tile_offsets_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "TileOffsets tag not found");
    }
    if (!optional::is_value_present(tile_byte_counts_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "TileByteCounts tag not found");
    }
    
    // Extract tile dimensions
    tile_width_ = optional::unwrap_value(tile_width_val);
    tile_height_ = optional::unwrap_value(tile_length_val);
    
    tile_depth_ = 1;
    if constexpr (TagSpec::template has_tag<TagCode::TileDepth>()) {
        tile_depth_ = optional::extract_tag_or<TagCode::TileDepth, TagSpec>(
            metadata, uint32_t{1}
        );
    }
    
    // Copy vector data (reuses allocation if size matches)
    const auto& offsets = optional::unwrap_value(tile_offsets_val);
    const auto& byte_counts = optional::unwrap_value(tile_byte_counts_val);
    
    tile_offsets_.assign(offsets.begin(), offsets.end());
    tile_byte_counts_.assign(byte_counts.begin(), byte_counts.end());
    
    compression_ = optional::extract_tag_or<TagCode::Compression, TagSpec>(
        metadata, CompressionScheme::None
    );
    
    predictor_ = Predictor::None;
    if constexpr (TagSpec::template has_tag<TagCode::Predictor>()) {
        predictor_ = optional::extract_tag_or<TagCode::Predictor, TagSpec>(
            metadata, Predictor::None
        );
    }
    
    // Calculate derived values
    tiles_across_ = (shape_.image_width() + tile_width_ - 1) / tile_width_;
    tiles_down_ = (shape_.image_height() + tile_height_ - 1) / tile_height_;
    tiles_deep_ = (shape_.image_depth() + tile_depth_ - 1) / tile_depth_;
    
    return Ok();
}

template <typename PixelType>
const ImageShape& TiledImageInfo<PixelType>::shape() const noexcept { 
    return shape_; 
}

template <typename PixelType>
uint32_t TiledImageInfo<PixelType>::tile_width() const noexcept { 
    return tile_width_; 
}

template <typename PixelType>
uint32_t TiledImageInfo<PixelType>::tile_height() const noexcept { 
    return tile_height_; 
}

template <typename PixelType>
uint32_t TiledImageInfo<PixelType>::tile_depth() const noexcept { 
    return tile_depth_; 
}

template <typename PixelType>
uint32_t TiledImageInfo<PixelType>::tiles_across() const noexcept { 
    return tiles_across_; 
}

template <typename PixelType>
uint32_t TiledImageInfo<PixelType>::tiles_down() const noexcept { 
    return tiles_down_; 
}

template <typename PixelType>
uint32_t TiledImageInfo<PixelType>::tiles_deep() const noexcept { 
    return tiles_deep_; 
}

template <typename PixelType>
std::size_t TiledImageInfo<PixelType>::num_tiles() const noexcept { 
    return tile_offsets_.size(); 
}

template <typename PixelType>
CompressionScheme TiledImageInfo<PixelType>::compression() const noexcept { 
    return compression_; 
}

template <typename PixelType>
Predictor TiledImageInfo<PixelType>::predictor() const noexcept { 
    return predictor_; 
}

template <typename PixelType>
Result<TileInfo> TiledImageInfo<PixelType>::get_tile_info(
    uint32_t tile_x, uint32_t tile_y, uint32_t plane) const noexcept {
    return get_tile_info_3d(tile_x, tile_y, 0, plane);
}

template <typename PixelType>
Result<TileInfo> TiledImageInfo<PixelType>::get_tile_info_3d(
    uint32_t tile_x, uint32_t tile_y, uint32_t tile_z, uint32_t plane) const noexcept {
    
    if (tile_x >= tiles_across_ || tile_y >= tiles_down_ || tile_z >= tiles_deep_) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Tile coordinates out of bounds");
    }
    
    // For planar configuration, tiles are organized by plane:
    // [plane0_tile0, plane0_tile1, ..., plane1_tile0, plane1_tile1, ...]
    // For 3D, tiles are organized as [z=0 tiles, z=1 tiles, ...]
    uint32_t tiles_per_slice = tiles_across_ * tiles_down_;
    uint32_t tiles_per_plane = tiles_per_slice * tiles_deep_;
    uint32_t tile_index;
    
    if (shape_.planar_configuration() == PlanarConfiguration::Planar) {
        if (plane >= shape_.samples_per_pixel()) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Plane index out of bounds");
        }
        tile_index = plane * tiles_per_plane + tile_z * tiles_per_slice + tile_y * tiles_across_ + tile_x;
    } else {
        // Chunky: ignore plane parameter, all data in one tile
        tile_index = tile_z * tiles_per_slice + tile_y * tiles_across_ + tile_x;
    }
    
    if (tile_index >= tile_offsets_.size()) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Tile index out of bounds");
    }
    
    TileInfo info;
    info.tile_x = tile_x;
    info.tile_y = tile_y;
    info.tile_z = tile_z;
    info.pixel_x = tile_x * tile_width_;
    info.pixel_y = tile_y * tile_height_;
    info.pixel_z = tile_z * tile_depth_;
    info.width = std::min(tile_width_, shape_.image_width() - info.pixel_x);
    info.height = std::min(tile_height_, shape_.image_height() - info.pixel_y);
    info.depth = std::min(tile_depth_, shape_.image_depth() - info.pixel_z);
    info.offset = tile_offsets_[tile_index];
    info.byte_count = tile_byte_counts_[tile_index];
    info.tile_index = tile_index;
    
    return Ok(info);
}

template <typename PixelType>
uint32_t TiledImageInfo<PixelType>::tiles_per_plane() const noexcept {
    return tiles_across_ * tiles_down_;
}

// ============================================================================
// StrippedImageInfo Member Function Implementations
// ============================================================================

template <typename PixelType>
StrippedImageInfo<PixelType>::StrippedImageInfo() noexcept
    : shape_()
    , rows_per_strip_(0)
    , strip_offsets_()
    , strip_byte_counts_()
    , compression_(CompressionScheme::None)
    , predictor_(Predictor::None)
    , num_strips_(0) {}

template <typename PixelType>
template <typename TagSpec>
    requires StrippedImageTagSpec<TagSpec>
Result<void> StrippedImageInfo<PixelType>::update_from_metadata(
    const ExtractedTags<TagSpec>& metadata) noexcept {
    
    // Extract common image shape first
    auto shape_result = shape_.update_from_metadata(metadata);
    if (shape_result.is_error()) [[unlikely]] {
        return shape_result;
    }

    // Validate pixel type
    auto format_validation = shape_.validate_pixel_type<PixelType>();
    if (format_validation.is_error()) [[unlikely]] {
        return format_validation;
    }
    
    // Extract strip-specific fields
    auto rows_per_strip_val = metadata.template get<TagCode::RowsPerStrip>();
    auto strip_offsets_val = metadata.template get<TagCode::StripOffsets>();
    auto strip_byte_counts_val = metadata.template get<TagCode::StripByteCounts>();
    
    // Validation
    if (!optional::is_value_present(rows_per_strip_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "RowsPerStrip tag not found");
    }
    if (!optional::is_value_present(strip_offsets_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "StripOffsets tag not found");
    }
    if (!optional::is_value_present(strip_byte_counts_val)) [[unlikely]] {
        return Err(Error::Code::InvalidTag, "StripByteCounts tag not found");
    }
    
    // Extract strip dimensions
    rows_per_strip_ = optional::unwrap_value(rows_per_strip_val);
    
    // Copy vector data (reuses allocation if size matches)
    const auto& offsets = optional::unwrap_value(strip_offsets_val);
    const auto& byte_counts = optional::unwrap_value(strip_byte_counts_val);
    
    strip_offsets_.assign(offsets.begin(), offsets.end());
    strip_byte_counts_.assign(byte_counts.begin(), byte_counts.end());
    
    compression_ = optional::extract_tag_or<TagCode::Compression, TagSpec>(
        metadata, CompressionScheme::None
    );
    
    predictor_ = Predictor::None;
    if constexpr (TagSpec::template has_tag<TagCode::Predictor>()) {
        predictor_ = optional::extract_tag_or<TagCode::Predictor, TagSpec>(
            metadata, Predictor::None
        );
    }
    
    // Calculate derived values
    num_strips_ = static_cast<uint32_t>(strip_offsets_.size());
    
    return Ok();
}

template <typename PixelType>
const ImageShape& StrippedImageInfo<PixelType>::shape() const noexcept { 
    return shape_; 
}

template <typename PixelType>
uint32_t StrippedImageInfo<PixelType>::rows_per_strip() const noexcept { 
    return rows_per_strip_; 
}

template <typename PixelType>
uint32_t StrippedImageInfo<PixelType>::num_strips() const noexcept { 
    return num_strips_; 
}

template <typename PixelType>
CompressionScheme StrippedImageInfo<PixelType>::compression() const noexcept { 
    return compression_; 
}

template <typename PixelType>
Predictor StrippedImageInfo<PixelType>::predictor() const noexcept { 
    return predictor_; 
}

template <typename PixelType>
Result<StripInfo> StrippedImageInfo<PixelType>::get_strip_info(
    uint32_t strip_index, uint32_t plane) const noexcept {
    
    // For planar configuration, calculate strips per plane
    uint32_t strips_per_plane = (shape_.image_height() + rows_per_strip_ - 1) / rows_per_strip_;
    uint32_t actual_strip_index;
    
    if (shape_.planar_configuration() == PlanarConfiguration::Planar) {
        if (plane >= shape_.samples_per_pixel()) [[unlikely]] {
            return Err(Error::Code::OutOfBounds, "Plane index out of bounds");
        }
        actual_strip_index = plane * strips_per_plane + strip_index;
    } else {
        actual_strip_index = strip_index;
    }
    
    if (actual_strip_index >= num_strips_) [[unlikely]] {
        return Err(Error::Code::OutOfBounds, "Strip index out of bounds");
    }
    
    StripInfo info;
    info.strip_index = actual_strip_index;
    info.pixel_y = strip_index * rows_per_strip_;  // Use logical strip_index for Y position
    info.width = shape_.image_width();
    info.height = std::min(rows_per_strip_, shape_.image_height() - info.pixel_y);
    info.offset = strip_offsets_[actual_strip_index];
    info.byte_count = strip_byte_counts_[actual_strip_index];
    
    return Ok(info);
}

template <typename PixelType>
uint32_t StrippedImageInfo<PixelType>::strips_per_plane() const noexcept {
    return (shape_.image_height() + rows_per_strip_ - 1) / rows_per_strip_;
}

} // namespace tiffconcept
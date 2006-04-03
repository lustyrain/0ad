// texture loaders and support functions
//
// Copyright (c) 2003-2005 Jan Wassenberg
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// Contact info:
//   Jan.Wassenberg@stud.uni-karlsruhe.de
//   http://www.stud.uni-karlsruhe.de/~urkt/

/*

Introduction
------------

This module allows reading/writing 2d images in various file formats and
encapsulates them in Tex objects.
It supports converting between pixel formats; this is to an extent done
automatically when reading/writing. Provision is also made for flipping
all images to a default orientation.


Format Conversion
-----------------

Image file formats have major differences in their native pixel format:
some store in BGR order, or have rows arranged bottom-up.
We must balance runtime cost/complexity and convenience for the
application (not dumping the entire problem on its lap).
That means rejecting really obscure formats (e.g. right-to-left pixels),
but converting everything else to uncompressed RGB "plain" format
except where noted in enum TexFlags (1).

Note: conversion is implemented as a pipeline: e.g. "DDS decompress +
vertical flip" would be done by decompressing to RGB (DDS codec) and then
flipping (generic transform). This is in contrast to all<->all
conversion paths: that would be much more complex, if more efficient.

Since any kind of preprocessing at runtime is undesirable (the absolute
priority is minimizing load time), prefer file formats that are
close to the final pixel format.

1) one of the exceptions is S3TC compressed textures. glCompressedTexImage2D
   requires these be passed in their original format; decompressing would be
   counterproductive. In this and similar cases, Tex.flags indicates such
   deviations from the plain format.


Default Orientation
-------------------

After loading, all images (except DDS, because its orientation is
indeterminate) are automatically converted to the global row
orientation: top-down or bottom-up, as specified by
tex_set_global_orientation. If that isn't called, the default is top-down
to match Photoshop's DDS output (since this is meant to be the
no-preprocessing-required optimized format).
Reasons to change it might be to speed up loading bottom-up
BMP or TGA images, or to match OpenGL's convention for convenience;
however, be aware of the abovementioned issues with DDS.

Rationale: it is not expected that this will happen at the renderer layer
(a 'flip all texcoords' flag is too much trouble), so the
application would have to do the same anyway. By taking care of it here,
we unburden the app and save time, since some codecs (e.g. PNG) can
flip for free when loading.


Codecs / IO Implementation
--------------------------

To ease adding support for new formats, they are organized as codecs.
The interface aims to minimize code duplication, so it's organized
following the principle of "Template Method" - this module both
calls into codecs, and provides helper functions that they use.

IO is done via VFS, but the codecs are decoupled from this and
work with memory buffers. Access to them is endian-safe.

When "writing", the image is put into an expandable memory region.
This supports external libraries like libpng that do not know the
output size beforehand, but avoids the need for a buffer between
library and IO layer. Read and write are zero-copy.

*/

#ifndef TEX_H__
#define TEX_H__

#include "../handle.h"

// flags describing the pixel format. these are to be interpreted as
// deviations from "plain" format, i.e. uncompressed RGB.
enum TexFlags
{
	// flags & TEX_DXT is a field indicating compression.
	// if 0, the texture is uncompressed;
	// otherwise, it holds the S3TC type: 1,3,5 or DXT1A.
	// not converted by default - glCompressedTexImage2D receives
	// the compressed data.
	TEX_DXT = 0x7,	// mask
	// we need a special value for DXT1a to avoid having to consider
	// flags & TEX_ALPHA to determine S3TC type.
	// the value is arbitrary; do not rely on it!
	DXT1A = 7,

	// indicates B and R pixel components are exchanged. depending on
	// flags & TEX_ALPHA or bpp, this means either BGR or BGRA.
	// not converted by default - it's an acceptable format for OpenGL.
	TEX_BGR = 0x08,

	// indicates the image contains an alpha channel. this is set for
	// your convenience - there are many formats containing alpha and
	// divining this information from them is hard.
	// (conversion is not applicable here)
	TEX_ALPHA = 0x10,

	// indicates the image is 8bpp greyscale. this is required to
	// differentiate between alpha-only and intensity formats.
	// not converted by default - it's an acceptable format for OpenGL.
	TEX_GREY = 0x20,

	// flags & TEX_ORIENTATION is a field indicating orientation,
	// i.e. in what order the pixel rows are stored.
	//
	// tex_load always sets this to the global orientation
	// (and flips the image accordingly).
	// texture codecs may in intermediate steps during loading set this
	// to 0 if they don't know which way around they are (e.g. DDS),
	// or to whatever their file contains.
	TEX_BOTTOM_UP = 0x40,
	TEX_TOP_DOWN  = 0x80,
	TEX_ORIENTATION = TEX_BOTTOM_UP|TEX_TOP_DOWN,	// mask

	// indicates the image data includes mipmaps. they are stored from lowest
	// to highest (1x1), one after the other.
	// (conversion is not applicable here)
	TEX_MIPMAPS = 0x100
};


// stores all data describing an image.
// we try to minimize size, since this is stored in OglTex resources
// (which are big and pushing the h_mgr limit).
struct Tex
{
	// H_Mem handle to image data. note: during the course of transforms
	// (which may occur when being loaded), this may be replaced with
	// a Handle to a new buffer (e.g. if decompressing file contents).
	Handle hm;

	// offset to image data in file. this is required since
	// tex_get_data needs to return the pixels, but mem_get_ptr(hm)
	// returns the actual file buffer. zero-copy load and
	// write-back to file is also made possible.
	size_t ofs;

	uint w : 16;
	uint h : 16;
	uint bpp : 16;

	// see TexFlags and "Format Conversion" in docs.
	uint flags : 16;
};


// set the orientation (either TEX_BOTTOM_UP or TEX_TOP_DOWN) to which
// all loaded images will automatically be converted
// (excepting file formats that don't specify their orientation, i.e. DDS).
// see "Default Orientation" in docs.
extern void tex_set_global_orientation(int orientation);


//
// open/close
//

// indicate if <filename>'s extension is that of a texture format
// supported by tex_load. case-insensitive.
//
// rationale: tex_load complains if the given file is of an
// unsupported type. this API allows users to preempt that warning
// (by checking the filename themselves), and also provides for e.g.
// enumerating only images in a file picker.
// an alternative might be a flag to suppress warning about invalid files,
// but this is open to misuse.
extern bool tex_is_known_extension(const char* filename);

// load the specified image from file into the given Tex object.
// currently supports BMP, TGA, JPG, JP2, PNG, DDS.
extern LibError tex_load(const char* fn, Tex* t, uint file_flags = 0);

// store the given image data into a Tex object; this will be as if
// it had been loaded via tex_load.
//
// rationale: support for in-memory images is necessary for
//   emulation of glCompressedTexImage2D and useful overall.
//   however, we don't want to  provide an alternate interface for each API;
//   these would have to be changed whenever fields are added to Tex.
//   instead, provide one entry point for specifying images.
// note: since we do not know how <img> was allocated, the caller must do
//   so (after calling tex_free, which is required regardless of alloc type).
//
// we need only add bookkeeping information and "wrap" it in
// our Tex struct, hence the name.
extern LibError tex_wrap(uint w, uint h, uint bpp, uint flags, void* img, Tex* t);

// free all resources associated with the image and make further
// use of it impossible.
extern LibError tex_free(Tex* t);


//
// modify image
//

// change <t>'s pixel format by flipping the state of all TEX_* flags
// that are set in transforms.
extern LibError tex_transform(Tex* t, uint transforms);

// change <t>'s pixel format to the new format specified by <new_flags>.
// (note: this is equivalent to tex_transform(t, t->flags^new_flags).
extern LibError tex_transform_to(Tex* t, uint new_flags);


//
// return image information
//

// since Tex is a struct, its fields are accessible to callers.
// this is more for C compatibility than convenience; the following should
// be used instead of direct access to the corresponding fields because
// they take care of some dirty work.

// returns a pointer to the image data (pixels), taking into account any
// header(s) that may come before it. see Tex.hm comment above.
extern u8* tex_get_data(const Tex* t);

// return total byte size of the image pixels. (including mipmaps!)
// this is preferable to calculating manually because it's
// less error-prone (e.g. confusing bits_per_pixel with bytes).
extern size_t tex_img_size(const Tex* t);


//
// image writing
//

// return the minimum header size (i.e. offset to pixel data) of the
// file format indicated by <fn>'s extension (that is all it need contain:
// e.g. ".bmp"). returns 0 on error (i.e. no codec found).
// this can be used to optimize calls to tex_write: when allocating the
// buffer that will hold the image, allocate this much extra and
// pass the pointer as base+hdr_size. this allows writing the header
// directly into the output buffer and makes for zero-copy IO.
extern size_t tex_hdr_size(const char* fn);

// write the specified texture to disk.
// note: <t> cannot be made const because the image may have to be
// transformed to write it out in the format determined by <fn>'s extension.
extern LibError tex_write(Tex* t, const char* fn);


// internal use only:
extern LibError tex_validate(const Tex* t);

// check if the given texture format is acceptable: 8bpp grey,
// 24bpp color or 32bpp color+alpha (BGR / upside down are permitted).
// basically, this is the "plain" format understood by all codecs and
// tex_codec_plain_transform.
// return 0 if ok, otherwise negative error code (but doesn't warn;
// caller is responsible for using CHECK_ERR et al.)
extern LibError tex_validate_plain_format(uint bpp, uint flags);


// indicate if the orientation specified by <src_flags> matches
// dst_orientation (if the latter is 0, then the global_orientation).
// (we ask for src_flags instead of src_orientation so callers don't
// have to mask off TEX_ORIENTATION)
extern bool tex_orientations_match(uint src_flags, uint dst_orientation);

typedef void (*MipmapCB)(uint level, uint level_w, uint level_h,
	const u8* level_data, size_t level_data_size, void* ctx);

// special value for levels_to_skip: the callback will only be called
// for the base mipmap level (i.e. 100%)
const int TEX_BASE_LEVEL_ONLY = -1;

extern void tex_util_foreach_mipmap(uint w, uint h, uint bpp, const u8* restrict data,
	int levels_to_skip, uint data_padding, MipmapCB cb, void* restrict ctx);


#endif	// TEX_H__

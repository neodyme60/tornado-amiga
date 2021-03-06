Tools
===

The Tornado framework provides a series of tools that will help you convert your source data into formats suitable for use in your production.

Image converters
---------

* ```anim_delta``` 
* ```bmp2bpl```
* ```png2raw```

----
```anim_delta``` transforms a color indexed PNG image into a series of delta-encoded frames to be used by the ```anim_replay``` code. 

You can see it in action in the ```splash``` example. All the images from the animation were put together sequentially using [ImageMagick](https://imagemagick.org/index.php)'s ```append``` command and the image was later converted to palette mode.

The data is encoded as follows:

* Palette in [LoadRGB32](http://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_3._guide/node02FB.html) format.
* Big endian ```uint32_t``` containing the number of frames.
* Frame 0.
* Delta frames.

A delta frame consists of rows. Each row starts with a 32bit bitmap. A bit set to 1 means a 10 pixel segment was stored. Following the bitmap are all the stored segments.

To recreate frame ```n``` from frame ```n - 1``` all we have to do is copy the new segments at the corresponding offset.

Two things to note:
* Frame 0 is stored as is, without any bitmaps.
* The tool is designed to work images that are 320 pixels wide.

Let's convert a PNG file with our animation frames:

```
mmendez$ anim_delta -b -h 180 -i capsule_anim_plte.png -o capsule_anim_plte.raw
Image information: W: 320, H: 17460, Bit depth: 8, Colours: 256, Frames: 96
Original size: 5587200.
Encoded size: 2442500 (43.715992%).
Palette size: 3080.
```

* ```-b``` means that there's a black frame at the end that should be ignored. The reason for this is to force colour 0 to be black when converting the image to indexed colour and thus avoid having a non-black border when replaying the animation.
* ```-h``` defines the frame height. Because we work with a single image, the converter has no way of knowing how tall each frame is supposed to be.
* ```-i``` and ```-o``` set the input and output files, respectively.

Later we will see how to compress this file to further reduce its size.

---
```bmp2bpl``` converts [BMP](https://en.wikipedia.org/wiki/BMP_file_format) images to planar data, either standard bitplanes or sprites.

While most of the effects you are going to write will make use of chunky buffers, sometimes you need data in planar format, e.g. when using [Sprites](Sprites.md).

The tool takes two arguments: output mode and input file, and writes the result to ```stdout```.

```
mmendez$ bmp2bpl --help
Usage: bmp2bpl [bpl/spr] pic.bmp > bpls.raw
```

Let's convert the data used in the ```sprites``` example:

```
mmendez$ bmp2bpl spr sprite1.bmp > sprite1.raw
Number of colors: 256
WARNING: you requested sprites, please note this bitmap has palette larger than 16
Writting 2 sprites of 4 bitplanes, using pairs
```

---
```png2raw``` converts an 8 or 4 bit per pixel [PNG](https://en.wikipedia.org/wiki/Portable_Network_Graphics) image into a file containing:

* The palette in [LoadRGB32](http://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_3._guide/node02FB.html) format.
* The RAW pixel data.

You can optionally not save the palette or just save palette.

```
mmendez$ png2raw 
Usage: png2raw [-n] [-p] -i <input_file> -o <output_file>
       -n : Do not save palette.
       -p : Only save the palette.
```

Let's convert the background image used in the ```simple_screen``` example:

```
mmendez$ png2raw -i Capsule_logo.png -o Capsule_logo.raw
Image information: W: 320, H: 256, Bit depth: 8, Colours: 256
Palette size: 3080.
```

Audio converters
---------

* ```tndo_ddcpm```
* ```wav2samp```

---
```tndo_ddpcm``` is a lossy audio compressor based on [DPCM](https://en.wikipedia.org/wiki/Differential_pulse-code_modulation) with a focus on quality and low decompression cost.

The input is always a 16bit 2-channel [WAV](https://en.wikipedia.org/wiki/WAV) file and the ouput is a compressed TNDO audio file. Optionally, a preview file can be generated which is a 16bit 2-channel WAV file that contains the result of the compression and decompression of the source material.

Unlike the commonly used 4bit [IMA ADPCM](https://en.wikipedia.org/wiki/Adaptive_differential_pulse-code_modulation) format, used in many Amiga demos, ```Tornado DDPCM``` uses 6bit deltas which reduces the quantisation error. This means that the compression ratio is 2.4:1 instead of 4:1 for ADPCM, but the resulting audio quality is much higher.

##### Compression algorithm


First the left and right audio channels are deinterleaved and stored in separate buffers. Each of them is then compressed independently.

The encoder uses a simple linear predictor expressed as:

```
predicted = s[n - 2] + (s[n - 1] - s[n - 2]) * 2
```

To compute sample n, we use the predictor and a delta value, like this:

```
s[n] = predict(s[n - 2], s[n - 1]) + delta
```

Unlike ADPCM, the quantisation tables are calculated for a specific file. 1024 64-entry ```q_tables``` are used, each of which covering 1/1024th of the samples.

Each ```q_table``` is computed by generating a histogram of all the deltas required to calculate the samples in that region and then picking the top 64 ones.

To compress a frame we first store the first 2 samples verbatim. Then, for the remaining 48 samples:

 * Set the delta scaling factor to 1.
 * Compress a frame and calculate the cumulative noise.
 * Iterate through all the scaling factors (1 to 64) and choose the one that yielded the lowest cumulative error.
 * Store the compressed frame and return the scaling factor.

The scaling factor is kept in the ```s_table``` and used during decompression.

##### Decompression

In order to decompress a frame we need:

 * The compressed frame data (40 bytes).
 * The scaling factor for that frame.
 * The corresponding ```q_table``` that holds the quantised deltas.

With that information we can generate 50 16bit samples:

```c
// Decodes a single frame from src to dst using the provided q_table.
void decodeFrame(uint8_t *src, int16_t *dst, int16_t *q_table, uint8_t scale) {
        uint8_t unpacked[DDPCM_FRAME_NUMSAMPLES - 2];
        int16_t y1, y2, p;
        
        uint16_t *first = (uint16_t *) src;
        dst[0] = ntohs((int16_t) first[0]);
        dst[1] = ntohs((int16_t) first[1]);
        
        unpack6to8(&src[4], unpacked);
        unpack6to8(&src[10], &unpacked[8]);
        unpack6to8(&src[16], &unpacked[16]);
        unpack6to8(&src[22], &unpacked[24]);
        unpack6to8(&src[28], &unpacked[32]);
        unpack6to8(&src[34], &unpacked[40]);
        
        for(uint32_t i = 2; i < DDPCM_FRAME_NUMSAMPLES; i++) {
          y1 = dst[i - 2];
          y2 = dst[i - 1];
          p = predict(y1, y2);
          dst[i] = p + (q_table[unpacked[i - 2]] / scale);
        }
}
```

##### Compressing a file

We need to provide an input file, an output file and, optinally, a preview file name:

```
thunderball:ddpcm mmendez$ ./tndo_ddcpm -i brutalism_16_22050.wav -o brutalism_16_22050.tndo -p brutalism_16_22050_ddpcm.wav 

Tornado DDPCM encoder. (C) 2019 Miguel Mendez.

Audio data information:
-----------------------
Bits per sample: 16
Sample rate: 22050Hz
Size: 19756800

Per channel data:
-----------------
Number of samples: 4966400
Sample size: 2
Frame size: 50 samples
Number of frames: 99328
Frames per quantisation table: 97

Compressing left channel: 99327/99328
Compressing right channel: 99327/99328

Uncompressed size: 19756800 bytes
Compressed size: 8407040 bytes (262144 bytes in qtables, 198656 bytes in scale tables, 7946240 bytes in compressed frames)
Decompressing left channel: 99327/99328
Decompressing right channel: 99327/99328
thunderball:ddpcm mmendez$ ls -la brutalism_16_22050*
-rw-r--r--  1 mmendez  staff   8407128 24 Jul 17:55 brutalism_16_22050.tndo
-rw-r--r--  1 mmendez  staff  19798444 24 Jul 17:52 brutalism_16_22050.wav
-rw-r--r--  1 mmendez  staff  19865644 24 Jul 17:55 brutalism_16_22050_ddpcm.wav
```

You can read more about how to use these files in the [Music](Music.md) section.

---
```wav2samp``` converts 8bit mono [WAV](https://en.wikipedia.org/wiki/WAV) files to raw samples that can be used for e.g. splash screens. The tool automatically converts the samples to signed which the Amiga audio hardware requires.

We give it an input and out file, like this;

```
mmendez$ wav2samp -i herbert.wav -o herbert.raw
Sample information:
Bits per sample: 8
Sample rate: 11025
Size: 13824
```

The resulting file starts with a very minimalistic header with the necessary information to correctly replay the sample:

* Big endian ```uint32_t``` containing the sample rate.
* Big endiang ```uint32_t``` containing the size of the sample data.
* Sample data.

Data compression
----------------

```tndo_compress``` is the Tornado data compression tool. It implements a variant of the [LZW](https://en.wikipedia.org/wiki/LZW) and [LZSS](https://en.wikipedia.org/wiki/LZSS) compression algorithms with a focus on fast decompression.

The ```LZW``` implementation embeds the dictionary with the compressed data, thus transforming the decompression code into a trivial lookup and a memory copy.

Similarly, the ```LZSS``` decompressor is essentially a memory copy loop.

Compression ratios are typically between 10% and 40% worse than [ZLIB](https://zlib.net/), but decompression speeds are significantly faster and limited only by your Amiga's memory speed.

When tested with the [Canterbury Corpus](https://en.wikipedia.org/wiki/Canterbury_corpus) the results were satisfactory, achieving compression ratios comparable to other [Lempel-Ziv](https://en.wikipedia.org/wiki/Lempel-Ziv) implementations.

Let's pack the ```anim_delta``` encoded file:

```
mmendez$ tndo_compress -b -v -i capsule_anim_plte.raw -o capsule_anim_plte.tndo
--------------------------
Step 1 - Build dictionary.
--------------------------
Number of codes used: 16384
Maximum symbol length: 25
--------------------------------------------
Step 2 - Compress with pre-built dictionary.
--------------------------------------------
Uncompressed: 2445584, compressed: 1321228 (54.0%)
Maximum symbol length: 25
[...]
mmendez$ ls -la capsule_anim_plte.*
-rw-r--r--  1 mmendez  wheel  2445584 18 Jun 14:23 capsule_anim_plte.raw
-rw-r--r--  1 mmendez  wheel  1535216 18 Jun 15:26 capsule_anim_plte.tndo
```

* ```-b``` causes ```tndo_compress``` to try all compression settings and use the best one. It's the recommended way of using this tool. 
* ```-v``` enables verbose mode and will output progress data as the compressor works on the file.
* ```-i``` and ```-o``` are the input and output files, respectively.


File management
--------------------

* ```tndo_info```
* ```assemble_assets```

---
```tndo_info``` prints out the metadata contained in the header of a TNDO file.

```
mmendez$ tndo_info 
Usage: tndo_info file
```

Let's look at a file compressed with ```tndo_compress```:

```
mmendez$ tndo_info capsule_anim_plte.tndo 
File name: capsule_anim_plte.tndo
File type: Generic.
Compression: TNDO LZW.
Compressed size: 1535144 bytes
Uncompressed size: 2445584 bytes
```
---
```assemble_assets``` combines a series of files into a single container file. Its main use is to create a single data file with all the demo assets nicely packed into one and is usually done once development has ended and we want to build the release version of our demo. You will find it in the ```vfs``` directory of the tools section.

***Please note that this tool does not compress the data. You should compress your individual assets using ```tndo_compress``` prior to putting them together.***

This is how that assets for the demo [Brutalism](http://www.pouet.net/prod.php?which=81062) were put together:

```
mmendez$ sh build_container.sh
Tornado Demo System Asset Manager
(C) 2017-2019 Miguel Mendez

Assembling assets to brutalism.tndo.
Adding files: data/capsulflix.tndo data/capsulflix.p61 data/loading_640_360.tndo data/loading_music.tndo data/brut_28150_8.tndo data/fountain-palrgb.raw data/protomin_back_1.tndo data/protomin_back_2.tndo data/protomin_back_3.tndo data/protomin_back_4.tndo data/fountain-palrgb.raw data/vinyetas-pal-tex.tndo data/vinyetas_sprite2.tndo data/vinyetas_sprite1.tndo data/vinyetas_sprite4.tndo data/vinyetas_sprite3.tndo data/scrolls_v.tndo data/scrolls2_v.tndo data/scrolls_h.tndo data/scrolls_spr.tndo data/scrolls2_h.tndo data/scrolls_spr2.tndo data/breuer-palrgb.tndo data/breuer-shd.tndo data/breuer-tex-reordered.tndo data/scrolls_spr.tndo data/fractal_fondo1.tndo data/fractal_fondo2.tndo data/fractal_fondo3.tndo data/fractal_fondo4.tndo data/fractal_bola1.tndo data/fractal_bola2.tndo data/fractal_bola3.tndo data/fractal_bola4.tndo data/voxels_fondo1.tndo data/voxels_fondo2.tndo data/voxels_fondo3.tndo data/voxels_fondo4.tndo data/fractal_sprite1.tndo data/fractal_sprite2.tndo data/fractal_sprite3.tndo data/fractal_sprite4.tndo data/tab_swirl.raw data/voxels_rock.tndo data/voxels_david.tndo data/voxels_david_cubed.tndo data/fractal.tndo data/blur-gr-1.rle data/blur-gr-2.rle data/blur-gr-3.rle data/blur-gr-4.rle data/blur-gr-5.rle data/blur-gr-6.rle data/blur-gr-7.rle data/blur-gr-8.rle data/blur-gr-9.rle data/blur-gr-10.rle data/blur-gr-11.rle data/blur-gr-12.rle data/blur-gr-13.rle data/blur-gr-14.rle data/blur-gr-15.rle data/blur-gr-16.rle data/blur-gr-17.rle data/blur-gr-18.rle data/blur-gr-19.rle data/blur-gr-20.rle data/blur-gr-21.rle data/blur-gr-22.rle data/blur-gr-23.rle data/blur-gr-24.rle data/blur-gr-25.rle data/blur-gr-26.rle data/blur-gr-27.rle data/blur-gr-28.rle data/blur-gr-29.rle data/blur-gr-30.rle data/back_blur.tndo data/freeway-palrgb.tndo data/freeway-shd.tndo data/freeway-tex-reordered.tndo data/scrolls_spr.tndo data/zoom.tndo data/zoom2.tndo data/zoom_frame0.tndo data/zoom.pal data/layers_back_1.tndo data/layers_back_2.tndo data/layers_back_3.tndo data/layers_back_4.tndo data/layers_spr1_a.tndo data/layers_spr1_b.tndo data/layers_spr1_c.tndo data/layers_spr2_a.tndo data/layers_spr2_b.tndo data/layers_spr2_c.tndo data/layers_spr3_a.tndo data/layers_spr3_b.tndo data/layers_spr3_c.tndo data/layers_spr4_a.tndo data/layers_spr4_b.tndo data/layers_spr4_c.tndo data/endscroll.p61 data/endscroll_back.tndo data/endscroll_fntbmp1.tndo data/endscroll_intro.tndo 
Done.
mmendez$ ls -la brutalism.tndo 
-rw-r--r--  1 mmendez  staff  16251608 18 Jun 15:41 brutalism.tndo
```

To learn more about how to prepare your demo for release, please refer to the [Getting ready for release](GettingReadyForRelease.md) section.

Rocket integration
---

```track2h``` is a tool that converts [Rocket](https://github.com/rocket/rocket) track data to include files that you can use directly in your demo.

```
mmendez$ track2h 
Usage: track2h -[ifd] -s <scale> -t <trackfile> -h <output file>
-i : Emit integer values.
-f : Emit floating point values (defailt)
-d : Emit double values.
-s : Scaling factor.
```

Example:

```c
mmendez$ track2h -t sync_layers.x1.track -h layers_x1.h
mmendez$ head -20 layers_x1.h
/*
 * Generated by track2h. Source file: sync_layers.x1.track
 * Data output settings: Float.
 * Scale: 1.000000
 */

#ifndef SYNC_LAYERS_X1_TRACK
#define SYNC_LAYERS_X1_TRACK

float sync_layers_x1_track[] = { 100.000000, 99.871597, 99.493637, 98.876953, 98.032410, 96.970848, 95.703125, 94.240089, 92.592590, 90.771484, 
	88.787613, 86.651840, 84.375000, 81.967957, 79.441551, 76.806641, 74.074074, 71.254700, 68.359375, 65.398941, 
	62.384258, 59.326172, 56.235531, 53.123192, 50.000000, 46.876808, 43.764469, 40.673828, 37.615742, 34.601055, 
	31.640625, 28.745298, 25.925926, 23.193359, 20.558449, 18.032045, 15.625000, 13.348163, 11.212384, 9.228516, 
	7.407407, 5.759910, 4.296875, 3.029152, 1.967593, 1.123047, 0.506366, 0.128400, 0.000000, 0.000000, 
	0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 
	0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 
	0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 
	0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 
	0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, -100.000000, -99.741821, -98.989258, -97.775269, 
	-96.132812, -94.094849, -91.694336, -88.964233, -85.937500, -82.647095, -79.125977, -75.407104, -71.523438, -67.507935, 
```

For more information about how to work with Rocket, please refer to the [Rocket](Rocket.md) section.


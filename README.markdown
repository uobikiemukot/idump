# idump

tiny image viewer for framebuffer

## usage

 $ idump [-h] [-f] [-r angle] image

 $ cat image | idump

 $ wget -q -O - url | idump

## options

-	-h: show help
-	-f: fit image to display size (reduce only)
-	-r: rotate image (90 or 180 or 270)

## supported image format

-	jpeg by libjpeg
-	png by libpng
-	gif by libnsgif
-	bmp by libnsbmp
-	pnm by idump
-	tga/psd/hdr/pic by stb_image

## wrapper scripts

-	ndump: equal "wget -q -O - url | idump"
-	pdump: take multiple files as arguments

## license

The MIT License (MIT)

Copyright (c) 2014 haru <uobikiemukot at gmail dot com>

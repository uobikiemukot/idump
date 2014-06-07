# idump

tiny image viewer for framebuffer

## usage

 $ idump [-h] [-f] [-r angle] image

 $ cat image | idump

 $ wget -q -O - url | idump

## options

-	-h: show help
-	-f: fit image to display size
-	-r: rotate image (90 or 180 or 270)

## wrapper scripts

-	ndump: equal "wget -q -O - url | idump"
-	pdump: take multiple files as arguments

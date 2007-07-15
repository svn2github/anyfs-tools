/*
 *	image_files_descr.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

char *image_JPEG_surrect();
char *image_PNG_surrect();
char *image_BMP_surrect();

_declspec(dllexport) 
const char* image_JPEG_indicator="JPEG";
_declspec(dllexport) 
const char* image_PNG_indicator="PNG";
_declspec(dllexport) 
const char* image_BMP_indicator="BMP";

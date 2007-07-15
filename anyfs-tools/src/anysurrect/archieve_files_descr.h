/*
 *	archieve_files_descr.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

char *archieve_TAR_surrect();
char *archieve_ZIP_surrect();
char *archieve_RAR_surrect();

_declspec(dllexport) 
const char* archieve_TAR_indicator="TAR";
_declspec(dllexport) 
const char* archieve_ZIP_indicator="ZIP";
_declspec(dllexport) 
const char* archieve_RAR_indicator="RAR";

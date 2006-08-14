/*
 *      config.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

/*
 * For recognizing plain text you may select ANY newline by macros ANYLINE
 * or some from it by macroses DOSLINE, UNIXLINE, MACLINE
 */
#define ANYLINE
//#define DOSLINE
//#define UNIXLINE
//#define MACLINE

/*Limit of size for files in zip, size of which pointed at end of file*/
/*Search of the end of such files is very expensive operation*/
#define MAX_SIZE_IN_ZIP 500*1024

/*Limit of size for PDF streams, size of which not pointed at header*/
/*Search of the end of such streams (by key word "endstream") is very expensive operation*/
#define MAX_PDF_STREAM_SIZE 50*1024

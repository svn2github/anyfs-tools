/*
 *	filesystem_info_descr.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

char *filesystem_info_ext2fs_direct_blocks_links_surrect();
char *filesystem_info_ext2fs_indirect_blocks_links_surrect();
char *filesystem_info_ext2fs_double_indirect_blocks_links_surrect();

char *filesystem_info_ext2fs_group_info_surrect();

_declspec(dllexport) 
const char* filesystem_info_ext2fs_direct_blocks_links_indicator="EXT2FS_DIRECT_LINKS";
_declspec(dllexport) 
const char* filesystem_info_ext2fs_indirect_blocks_links_indicator="EXT2FS_INDIRECT_LINKS";
_declspec(dllexport) 
const char* filesystem_info_ext2fs_double_indirect_blocks_links_indicator="EXT2FS_DOUBLE_INDIRECT_LINKS";

_declspec(dllexport) 
const char* filesystem_info_ext2fs_group_info_indicator="EXT2FS_GROUPINFO";

_declspec(dllexport) 
const char* filesystem_info_ext2fs_group_info_opts="ext2fs";
void filesystem_info_ext2fs_group_info_parseopts
	(const int argc, const char* argv[]);
void filesystem_info_ext2fs_group_info_usage();


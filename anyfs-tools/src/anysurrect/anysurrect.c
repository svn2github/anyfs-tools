/*
 *	anysurrect.c
 *      CopyRight (C) 2006-2008
 *      	Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#define _LARGEFILE64_SOURCE

#include "any.h"
#include "super.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <sys/ioctl.h>
#include <linux/fd.h>

#include <dlfcn.h>

#include <assert.h>

#include <bitops.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "anysurrect.h"

#include "new_inode.h"
#include "block_map.h"
#include "progress.h"
#include "version.h"

#define BLKGETSIZE _IO(0x12,96) /* return device size /512 (long *arg) */

const char* default_list_types =
#include "surrectlist.conf"
;

const char* ext2fs_info_list_types =
"filesystem_info_ext2fs_direct_blocks_links "
"filesystem_info_ext2fs_indirect_blocks_links "
"filesystem_info_ext2fs_double_indirect_blocks_links "
"filesystem_info_ext2fs_inode_table "
"filesystem_info_ext2fs_group_info "
;

char* list_types = NULL;
char* list_types2 = NULL;

const char * program_name = "anysurrect";

char * libs = "";
int num_libs;
void **libs_handles;

struct progress_struct progress;

int list_all_types = 0;
int verbose = 0;
int quiet = 0;
int fromfirst = 0;
int mod_usages = 0;

const char *pathprefix = "";
const char *device_name;
const char *inode_table;
const char *input_inode_table = NULL;
unsigned long input_blocksize = 0;

any_size_t device_blocks = 0;

mode_t dir_umask = 0002;
mode_t file_umask = 0002;

const mode_t default_mode = 0666;
const int default_text = 0;
const char *default_indicator = "UNKNOWN";

typedef char *surrect_function_t();
typedef void parseopts_function_t(const int argc, const char* argv[]);
typedef void usage_function_t();

surrect_function_t** surrects;
const mode_t **modes;
const int **texts;
const char ***indicators;
char **typelines;

const char ***modoptions;
parseopts_function_t** parseopts;
usage_function_t** modusages;

const char ***modoptions2;
parseopts_function_t** parseopts2;
usage_function_t** modusages2;

static int ind_type=-1;
int num_types = 0; 


int num_types2 = 0; 
surrect_function_t** surrects2;
const mode_t **modes2;

static void usage(void)
{
	fprintf(stderr, _(
"Usage: %s [-b blocksize] [-i input_inode_table] [-p path_prefix]\n"
"\t[-u file_umask] [-U dir_umask] [-fqvV] [-hH] [-g plug-ins]\n"
"\t[-e] [-t list_of_types] [-T list_of_types] [-l] device inode_table\n"
"\t[-- [module module_options [--]] ...]\n"),
			program_name);
	exit(1);
}

extern char *concat_strings(int n, ...);

unsigned long *block_bitmap;
struct frags_list *frags_list;
struct frags_list *file_template_frags_list;
struct frags_list *file_frags_list;

unsigned long blocks_before_frag;
struct frags_list *cur_frag;

/*
int redirect_frags(struct frags_list *frags_list, unsigned long after,
		unsigned long block);
 */

int fd;
uint32_t blocksize;
uint64_t blocksize64;
int 	 log2blocksize;
uint32_t bitsblocksize;
uint64_t bitsblocksize64;
uint32_t nbitsblocksize;
uint64_t nbitsblocksize64;

struct io_buffer io_buffer = {NULL, -1, 0};

struct frags_list *copy_file_template_frags_list = NULL;

inline int set_block(any_ssize_t s_block)
{
	cut_frags(&file_template_frags_list, 0, s_block);
	if (copy_file_template_frags_list)
		cut_frags(&copy_file_template_frags_list, 0, s_block);
	io_buffer.size = 0;

	/*
	printf ("%d, %d\n", file_template_frags_list->frag.fr_start,
			file_template_frags_list->frag.fr_length);
*/
	return (file_template_frags_list)?0:1;
}

any_off_t cur_offset = 0;

any_off_t _fd_seek(any_off_t offset, int whence)
{
	if (whence==SEEK_CUR)
		offset += cur_offset;
	
	if (whence==SEEK_END)
		offset += fd_size();

	if ( ( blocks_before_frag << get_log2blocksize() ) > offset || !cur_frag )
	{
		blocks_before_frag = 0;
		cur_frag = file_frags_list;
	}

	while ( ( ( blocks_before_frag + cur_frag->frag.fr_length ) <<
					get_log2blocksize() )<=offset )
	{
		blocks_before_frag += cur_frag->frag.fr_length;
		NEXT_FRAG(cur_frag);
		if ( !cur_frag )
		{
			if ( ( blocks_before_frag << get_log2blocksize() )<offset )
				return -1;
			lseek64(fd, 0, SEEK_END);
			cur_offset = offset;
			return cur_offset;
		}
	}

	lseek64(fd, ( ((any_size_t) cur_frag->frag.fr_start - blocks_before_frag) << 
				get_log2blocksize()) + offset, 
			SEEK_SET);

	cur_offset = offset;
	
	return cur_offset;
}

void anysurrect_file(struct any_sb_info *info, char *dirn, mode_t mode)
{
	int res;
	struct any_inode *dir_inode;
	struct any_inode *inode;
	struct any_dirent **pdirent;
	struct any_dirent *newdirent;
	uint32_t newino;
	uint32_t dirino;
	char name_buffer[1024];

	char *dir = concat_strings(2, pathprefix, dirn);
	
	res = mkpathino(dir, 1, info, &dirino);

	free(dir);
	
	res = any_new_inode(info, S_IFREG | ( mode & (~file_umask & 0777)  ), 
			NULL, dirino, &newino);
	if (res) 
	{
		fprintf (stderr, _("error while creating inode\n"));
		exit(1);
	}

	res = snprintf (name_buffer, 1024, "inode_%d_block_%ld",
			newino, get_block() );
	if (res<0)
	{
		fprintf(stderr, _("error while format filename\n"));
		exit(1);
	}

	newdirent = calloc(sizeof(struct any_dirent), 1);
	if (!newdirent)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}

        dir_inode = info->si_inode_table + dirino;
	
	pdirent = &dir_inode->i_info.dir->d_dirent;

	for (; *pdirent; pdirent=&(*pdirent)->d_next);

	dir_inode->i_info.dir->d_ndirents++;
	(*pdirent) = newdirent;
	(*pdirent)->d_name = strdup(name_buffer);
	(*pdirent)->d_inode = newino;
	
	inode = info->si_inode_table + newino;
	inode->i_size = fd_seek(0, SEEK_CUR);
	
	pick_frags(&file_frags_list, 0,
			( (inode->i_size-1) >> get_log2blocksize()) + 1 );

/*	printf ("%d, %d\n", file_frags_list->frag.fr_start,
			file_frags_list->frag.fr_length);
*/	
	frags_list2array (file_frags_list, &inode->i_info.file_frags);
/*
	printf ("%d\n", inode->i_info.file_frags->fr_nfrags);
	printf ("%d, %d\n", inode->i_info.file_frags->fr_frags->fr_start,
			inode->i_info.file_frags->fr_frags->fr_length);
			*/
}

struct any_sb_info *info_for_anysurrect_frags_list;

void anysurrect_frags_list(struct frags_list *l_file_frags_list,
		any_size_t size, const char *dmes)
{
	const char *mes;
	int type;
	struct frags_list *old_copy_file_template_frags_list =
		copy_file_template_frags_list;
	struct frags_list *old_file_template_frags_list =
		file_template_frags_list;
	struct frags_list *old_file_frags_list =
		file_frags_list;
	file_template_frags_list = l_file_frags_list;
	copy_file_template_frags_list = NULL;

	unsigned long old_blocks_before_frag = blocks_before_frag;
	struct frags_list *old_cur_frag = cur_frag;

	struct any_sb_info *info = info_for_anysurrect_frags_list;
	int goodsize = 0;

	io_buffer.size = 0;

	char *old_pathprefix = pathprefix;
	pathprefix = concat_strings(3, pathprefix, dmes, "/");

	for (type=0; type<num_types2; type++)
	{
		if (copy_file_template_frags_list)
		{
			file_frags_list = copy_file_template_frags_list;
			copy_file_template_frags_list = NULL;
		}
		else
			copy_frags_list (file_template_frags_list, &file_frags_list);

		blocks_before_frag = 0;
		cur_frag = file_frags_list;
		fd_seek(0, SEEK_SET);
		{
			mes = surrects2[type](); 
			if (mes) { 
				mode_t mode = *modes2[type];
				char* dupmes = strdup(mes);
				if (verbose) printf("file %s, block=%ld, size=%lld (0x%llx)\n",
						mes, get_block(), fd_seek(0, SEEK_CUR), fd_seek(0, SEEK_CUR));
				if ( fd_seek(0, SEEK_CUR) == size ) goodsize = 1;
				anysurrect_file(info, dupmes, mode);
				free(dupmes);
				free_frags_list (file_frags_list);
			}
			else {	
				copy_file_template_frags_list = file_frags_list;
			}
		}
	}

	if (size && !goodsize)
	{
		if (copy_file_template_frags_list)
		{
			file_frags_list = copy_file_template_frags_list;
			copy_file_template_frags_list = NULL;
		}
		else
			copy_frags_list (file_template_frags_list, &file_frags_list);

		blocks_before_frag = 0;
		cur_frag = file_frags_list;
		fd_seek(size, SEEK_SET);

		mode_t mode = *modes2[ind_type];
		char* dupmes = strdup("UNKNOWN");
		if (verbose) printf("file %s, block=%ld, size=%lld (0x%llx)\n",
				mes, get_block(), fd_seek(0, SEEK_CUR), fd_seek(0, SEEK_CUR));
		if ( fd_seek(0, SEEK_CUR) == size ) goodsize = 1;
		anysurrect_file(info, dupmes, mode);
		free(dupmes);
		free_frags_list (file_frags_list);
	}

	if (copy_file_template_frags_list)
		free_frags_list(copy_file_template_frags_list);

	free_frags_list(l_file_frags_list);

	blocks_before_frag = old_blocks_before_frag;
	cur_frag = old_cur_frag;

	file_frags_list = old_file_frags_list;
	file_template_frags_list = old_file_template_frags_list;
	copy_file_template_frags_list = old_copy_file_template_frags_list;

	free(pathprefix);
	pathprefix = old_pathprefix;

	io_buffer.size = 0;
}

void anysurrect_fromblock(struct any_sb_info *info)
{
	char *mes;
	any_size_t max_size=1;

	int type;

	info_for_anysurrect_frags_list = info;

	static unsigned long *SKIP_TO_BLOCK = NULL;
	if (!SKIP_TO_BLOCK) 
	{
		SKIP_TO_BLOCK = calloc(num_types, sizeof(unsigned long) );
		
		if (!SKIP_TO_BLOCK)
		{
			fprintf (stderr, _("Not enough memory\n"));
			exit(1);
		}
	}

	if (!quiet && if_progress_updated(&progress,
				file_template_frags_list->frag.fr_start))
	{
		fwrite( typelines[0], 32, 1, stderr);
		fflush(stderr);
	}
	
	for (type=0; type<num_types; type++)
	{
		ind_type = type;
		int text = *texts[type];

		if (copy_file_template_frags_list)
		{
			file_frags_list = copy_file_template_frags_list;
			copy_file_template_frags_list = NULL;
		}
		else
			copy_frags_list (file_template_frags_list, &file_frags_list);

		blocks_before_frag = 0;
		cur_frag = file_frags_list;
		fd_seek(0, SEEK_SET);
		if (!text || (get_block()>=SKIP_TO_BLOCK[type]) ) {
			mes = surrects[type](); 
			if (mes) { 
				mode_t mode = *modes[type];
				char* dupmes = strdup(mes);
				if (verbose) printf("file %s, block=%ld, size=%lld (0x%llx)\n",
						mes, get_block(), fd_seek(0, SEEK_CUR), fd_seek(0, SEEK_CUR));
				max_size = max_t(any_size_t, max_size, fd_seek(0, SEEK_CUR) );
				anysurrect_file(info, dupmes, mode);
				free(dupmes);
				free_frags_list (file_frags_list);
			}
			else {	
				any_size_t pos = fd_seek(0, SEEK_CUR);
				if (text) SKIP_TO_BLOCK[type] = get_block() + 
					((pos-1) >> get_log2blocksize()) + 1;
				copy_file_template_frags_list = file_frags_list;
			}
		}
		else copy_file_template_frags_list = file_frags_list;
	}

	ind_type = -1;

	set_block( ( (max_size-1) >> get_log2blocksize() ) + 1 );

	if (!quiet && file_template_frags_list && if_progress_update(&progress,
				file_template_frags_list->frag.fr_start))
		fwrite( "\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b", 32, 1, stderr);
}

static void PRS(int argc, const char *argv[])
{
	int             c;
	int show_version_only = 0;
	char *          tmp;
	int end_opts = 0;

	list_types = (char*) default_list_types;
	list_types2 = (char*) default_list_types;

	while (!end_opts && (c = getopt (argc, (char**)argv,
		     "b:i:p:u:U:qvVfg:et:T:lhH-")) != EOF) {
		switch (c) {
		case 'b':
			input_blocksize = strtol(optarg, &tmp, 10);

			int log2 = 1;
			int b = input_blocksize;
			while ( (b = b>>1)>1 ) log2++;
			for (; log2; log2--) b = b<<1;
			if (b!=input_blocksize || input_blocksize<512 || *tmp) 
			{
				fprintf (stderr, 
						_("bad new blocksize. It is must be power of 2 and not less than 512\n"));
				exit(1);
			}
			break;

		case 'i':
			input_inode_table = optarg;
			break;

		case 'p':
			pathprefix = optarg;
			break;
			
		case 'u':
			file_umask = strtol(optarg, &tmp, 8);

			if (file_umask>0777 || *tmp) 
			{
				fprintf (stderr, 
_("Illegal mode for file umask. It must be 3 octal digits.\n"));
				exit(1);
			}
			break;
			
		case 'U':
			dir_umask = strtol(optarg, &tmp, 8);

			if (file_umask>0777 || *tmp) 
			{
				fprintf (stderr, 
_("Illegal mode for directory umask. It must be 3 octal digits.\n"));
				exit(1);
			}
			break;

		case 'q':
			quiet = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'V':
			show_version_only++;
			break;
		case 'f':
			fromfirst = 1;
			break;

		case 'g':
			libs = optarg;
			break;
			
		case 'e':
			list_types = (char*) ext2fs_info_list_types;
			break;

		case 't':
			list_types = optarg;
			break;

		case 'T':
			list_types2 = optarg;
			break;

		case 'l':
			list_all_types = 1;
			break;

		case 'H':
			mod_usages = 1;
			break;

		case '-':
			end_opts = 1;
			break;

		case 'h':
		default:
			usage();
		}
	}

	list_types = strdup(list_types);
	list_types2 = strdup(list_types2);
	libs = strdup(libs);

	if ( ( optind >= (argc-1) ) && !show_version_only && !list_all_types
			&& !mod_usages )
		usage();

	if (!quiet || show_version_only)
		fprintf (stderr, "anysurrect %s (%s)\n", ANYFSTOOLS_VERSION,
				ANYFSTOOLS_DATE);

	if (show_version_only) {
		exit(0);
	}
	
	if (!list_all_types && !mod_usages)
	{
		device_name = argv[optind++];
		inode_table = argv[optind++];
	}
}

void sigusr1_handler (int signal_number)
{       
	if (ind_type >= 0)
	{
		fwrite( "\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b", 32, 1, stderr);
		fwrite( typelines[ind_type], 32, 1, stderr);
	}
	fflush(stderr);
}

void sigint_handler (int signal_number)
{       
	if (ind_type >= 0)
	{
		fwrite( "\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b", 32, 1, stderr);
		fwrite( typelines[ind_type], 32, 1, stderr);
	}
	fprintf (stderr, "\nuser cancel\n");
	fflush(stderr);

	_exit(1);
}

void sigsegv_handler (int signal_number)
{       
	if (ind_type >= 0)
	{
		fwrite( "\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b"
			"\b\b\b\b\b\b\b\b", 32, 1, stderr);
		fwrite( typelines[ind_type], 32, 1, stderr);
	}
	fflush(stderr);
	fprintf (stderr, "\nSegmentation fault\n");

	abort();
}

int main (int argc, const char *argv[])
{
	int r;
	uint32_t rootino;
	struct any_sb_info *info;
	unsigned long bitmap_l;
	unsigned long *block_bitmap;

	char *begin;
	char *end;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif

	struct sigaction sa;
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &sigint_handler;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGSEGV);
	sigaddset(&sa.sa_mask, SIGUSR1);
	sigaction (SIGINT, &sa, NULL);

	struct sigaction sa1;
	memset (&sa1, 0, sizeof (sa1));
	sa1.sa_handler = &sigsegv_handler;
	sa1.sa_flags = SA_RESTART;
	sigemptyset(&sa1.sa_mask);
	sigaddset(&sa1.sa_mask, SIGINT);
	sigaddset(&sa1.sa_mask, SIGSEGV);
	sigaddset(&sa1.sa_mask, SIGUSR1);
	sigaction (SIGSEGV, &sa1, NULL);

	struct sigaction sa2;
	memset (&sa2, 0, sizeof (sa2));
	sa2.sa_handler = &sigusr1_handler;
	sa2.sa_flags = SA_RESTART;
	sigemptyset(&sa2.sa_mask);
	sigaddset(&sa2.sa_mask, SIGINT);
	sigaddset(&sa2.sa_mask, SIGSEGV);
	sigaddset(&sa2.sa_mask, SIGUSR1);
	sigaction (SIGUSR1, &sa2, NULL);

	PRS(argc, argv);
	
	num_libs = 1;
	begin = libs;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		num_libs++;
		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}
	
	libs_handles = (void**) 
		MALLOC( sizeof(void*) * num_libs );

	libs_handles[0] = dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);
	if (!libs_handles[0])
	{
		fprintf( stderr, "Error dlopen() main handle: %s\n",
				dlerror() );
		exit(1);
	}
	
	int lib = 1;
	begin = libs;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		char old = end[0];
		end[0] = '\0';

		libs_handles[lib] = dlopen(begin, RTLD_LAZY | RTLD_GLOBAL);
		if (!libs_handles[lib])
		{
			fprintf( stderr, "Error dlopen() %s: %s\n",
					begin, dlerror() );
			exit(1);
		}

		end[0] = old;
		lib++;

		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}

	if (list_all_types)
	{
		char self[1024];
		char buf[1024];
		
		r = readlink("/proc/self", self, 1023);
		if (r<0) exit(1);

		self[r] = '\0';
			
		r = snprintf(buf, 1024,
"for j in `for i in %s %s; do "
	"cat /proc/%s/maps | "
		"awk -- '/ r-xp.*'${i//\\//\\\\\\/}'$/ {print $6}';"
	" done`; do " 
	"echo; echo \"FILE SURRECTERS EXPORTED BY \\\"${j##*/}\\\" MODULE:\"; "
	"nm --defined-only -D \"$j\" | awk -- '/_surrect$/ "
	"{sub(/_surrect$/, \"\"); ORS=\" \"; print $3}'; echo; "
"done",
			program_name, libs, self);
		if (r<0) exit(1);

		pid_t child;
		if ( !( child = fork() ) )
		{
			execl ("/bin/sh", "/bin/sh", "-c", buf, NULL);
			exit(1);
		}

		waitpid(child, NULL, 0);

		exit(0);
	}

	begin = list_types;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		num_types++;
		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}

	begin = list_types2;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		num_types2++;
		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}
	
	surrects = (surrect_function_t**) 
		MALLOC( sizeof(surrect_function_t*) * num_types );
	modes = (const mode_t**)
		MALLOC( sizeof(mode_t*) * num_types );
	texts = (const int**)
		MALLOC( sizeof(int*) * num_types );
	indicators = (const char***)
		MALLOC( sizeof(char**) * num_types );

	modoptions = (const char***)
		MALLOC( sizeof(char**) * num_types );
	parseopts = (parseopts_function_t**)
		MALLOC( sizeof(parseopts_function_t*) * num_types );
	modusages = (usage_function_t**)
		MALLOC( sizeof(usage_function_t*) * num_types );

	surrects2 = (surrect_function_t**) 
		MALLOC( sizeof(surrect_function_t*) * num_types2 );
	modes2 = (const mode_t**)
		MALLOC( sizeof(mode_t*) * num_types2 );

	modoptions2 = (const char***)
		MALLOC( sizeof(char**) * num_types2 );
	parseopts2 = (parseopts_function_t**)
		MALLOC( sizeof(parseopts_function_t*) * num_types2 );
	modusages2 = (usage_function_t**)
		MALLOC( sizeof(usage_function_t*) * num_types2 );

	int type = 0;
	begin = list_types;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		void *handle = NULL;
		int n;
		char buf[1024];

		char old = end[0];
		end[0] = '\0';

		n=snprintf(buf, 1024, "%s_surrect", begin);
		if (n<0) exit(1);

		for (lib=0; lib<num_libs; lib++)
		{
			surrects[type] = dlsym(libs_handles[lib], buf);
			if (surrects[type])
			{
				handle = libs_handles[lib];
				break;
			}
		}

		if (!handle)
		{
			fprintf (stderr,
					_("can't find %s surrecter function\n"),
					begin);
			exit(1);
		}

		n=snprintf(buf, 1024, "%s_mode", begin);
		if (n<0) exit(1);

		modes[type] = dlsym(handle, buf);
		if (!modes[type])
			modes[type] = &default_mode;

		n=snprintf(buf, 1024, "%s_text", begin);
		if (n<0) exit(1);

		texts[type] = dlsym(handle, buf);
		if (!texts[type])
			texts[type] = &default_text;

		n=snprintf(buf, 1024, "%s_indicator", begin);
		if (n<0) exit(1);

		indicators[type] = dlsym(handle, buf);
		if (!indicators[type])
			indicators[type] = &default_indicator;

		n=snprintf(buf, 1024, "%s_opts", begin);
		if (n<0) exit(1);

		modoptions[type] = dlsym(handle, buf);

		n=snprintf(buf, 1024, "%s_parseopts", begin);
		if (n<0) exit(1);

		parseopts[type] = dlsym(handle, buf);

		n=snprintf(buf, 1024, "%s_usage", begin);
		if (n<0) exit(1);

		modusages[type] = dlsym(handle, buf);

		end[0] = old;
		type++;

		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}
	
	type = 0;
	begin = list_types2;
	while ( begin[0] == ' ' ) begin++;
	while ( (end = strchr(begin, ' ')) || ( begin[0] && 
				(end = begin+strlen(begin)) ) )
	{
		void *handle = NULL;
		int n;
		char buf[1024];

		char old = end[0];
		end[0] = '\0';

		n=snprintf(buf, 1024, "%s_surrect", begin);
		if (n<0) exit(1);

		for (lib=0; lib<num_libs; lib++)
		{
			surrects2[type] = dlsym(libs_handles[lib], buf);
			if (surrects2[type])
			{
				handle = libs_handles[lib];
				break;
			}
		}

		if (!handle)
		{
			fprintf (stderr,
					_("can't find %s surrecter function\n"),
					begin);
			exit(1);
		}

		n=snprintf(buf, 1024, "%s_mode", begin);
		if (n<0) exit(1);

		modes2[type] = dlsym(handle, buf);
		if (!modes2[type])
			modes2[type] = &default_mode;

		int no_in_first_list = 1;
		int type1;

		for (type1=0; type1 < num_types; type1++)
		{
			if (surrects[type1] == surrects2[type])
			{
				no_in_first_list = 0;
				break;
			}
		}

		if (no_in_first_list)
		{
			n=snprintf(buf, 1024, "%s_opts", begin);
			if (n<0) exit(1);

			modoptions2[type] = dlsym(handle, buf);

			n=snprintf(buf, 1024, "%s_parseopts", begin);
			if (n<0) exit(1);

			parseopts2[type] = dlsym(handle, buf);

			n=snprintf(buf, 1024, "%s_usage", begin);
			if (n<0) exit(1);

			modusages2[type] = dlsym(handle, buf);
		}
		else
		{
			modoptions2[type] = NULL;
			parseopts2[type] = NULL;
			modusages2[type] = NULL;
		}

		end[0] = old;
		type++;

		begin = end;
		while ( begin[0] == ' ' ) begin++;
	}

	if (mod_usages)
	{
		for (type=0; type < num_types; type++)
			if (modusages[type])
				modusages[type](); 

		for (type=0; type < num_types2; type++)
			if (modusages2[type])
				modusages2[type](); 

		exit(0);
	}

	while (optind < argc)
	{
		int y=0;
		for (type=0; type < num_types; type++)
		{
			if ( modoptions[type] &&
					strcmp(*modoptions[type], argv[optind])==0 )
			{
				optind++;
				parseopts[type](argc, argv);
				y++;
				break;
			}
		}

		if (!y)
		for (type=0; type < num_types2; type++)
		{
			if ( modoptions2[type] &&
					strcmp(*modoptions2[type], argv[optind])==0 )
			{
				optind++;
				parseopts2[type](argc, argv);
				y++;
				break;
			}
		}

		if (!y)
		{
			fprintf(stderr, "Unknown module option %s\n", argv[optind]);
			fprintf(stderr, "Use -H for list known module options\n");
			exit(1);
		}
	}

	typelines = (char**)
		MALLOC( sizeof(char*) * num_types );

	type = 0;

	for (type=0; type<num_types; type++)
	{
		typelines[type] = (char*) MALLOC(33);

		typelines[type][32] = '\0';
		typelines[type][0] = ' ';
		typelines[type][1] = '[';
		int typeline_length = strlen(*indicators[type]);
		typeline_length = min_t(int, typeline_length, 29);
		memcpy(typelines[type]+2, *indicators[type], typeline_length);
		typelines[type][2+typeline_length] = ']';
		memset (typelines[type] + (3+typeline_length), ' ', 
				32 - (3+typeline_length) );
	}

	fd = open(device_name, O_RDONLY | O_LARGEFILE);
	if (fd==-1) {
		fprintf(stderr, _("Error opening file\n"));
		exit(1);
	}

	struct stat64 st;
	fstat64(fd, &st);
	if (st.st_mode & S_IFBLK)
	{
		if (ioctl(fd, BLKGETSIZE, &st.st_size) < 0) {
			perror("BLKGETSIZE");
			close(fd);
			return -EIO;
		}
		st.st_size*=512;
	}

	if (!input_inode_table)
	{
		if (!input_blocksize)
		{
			set_blocksize(512);

			while ( st.st_size >> get_log2blocksize() > 0xFFFFFF )
				set_blocksize(get_blocksize()*2);

			if ( get_blocksize()>4096 )
				set_blocksize(4096);
		}
		else
			set_blocksize(input_blocksize);


		if ( st.st_size >> get_log2blocksize() > 0xFFFFFFFFULL )
		{
			fprintf(stderr,
					_("Sorry, too large device.\n"));
		}

		uint32_t inodes = ( st.st_size >> get_log2blocksize() )/1024 + 256;

		r = alloc_it(&info, get_blocksize(), inodes);
		if (r<0) goto out;

		r = any_new_inode(info, S_IFDIR | (~dir_umask & 0777), NULL, 0, &rootino);
		if (r) goto free_out;

		if (rootino!=1)
		{
			fprintf (stderr, _("Error creating root inode\n"));
			exit(1);
		}
	}
	else
	{
		r = read_it (&info, (void*)input_inode_table);
		if (r)
		{
			fprintf(stderr,
					_("Error while reading inode table: %s\n"),
					(errno)?strerror(errno):_("bad format"));
			goto out;
		}
		
		if (input_blocksize && input_blocksize!=info->si_blocksize)
		{
			fprintf(stderr,
_("Specified input inode table has %lu blocksize,\n"
"  but specified blocksize by -b option is %lu\n"
"Use 'reblock' for changing blocksize of inode_table\n"),
	info->si_blocksize, input_blocksize);
			exit(1);
		}
		
		set_blocksize(info->si_blocksize);
	}
	
	any_size_t blocks = ((st.st_size-1) >> get_log2blocksize()) + 1;
	device_blocks = blocks;

	if (!blocks)
	{
			fprintf(stderr, _("It is not funny at all! I can do nothing with empty file.\n"));
			exit(1);
	}

	bitmap_l = ( blocks +
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);

	/*allocate memory for block bitmap*/
	block_bitmap =  (typeof(block_bitmap))
		calloc(bitmap_l*sizeof(unsigned long), 1);
	if (!block_bitmap)      
	{ 
		fprintf(stderr, _("Not enough memory\n"));
		exit(1); 
	}

	r = fill_block_bitmap (info, block_bitmap, blocks, 0);
	if (r) exit(r);

	if (fromfirst) clear_bit(0, block_bitmap);

	anysurrect_malloc_clean();
	
	create_frags_list(block_bitmap, blocks, &frags_list);

	io_buffer.buffer = malloc(get_blocksize());
	if (!io_buffer.buffer)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	
	copy_frags_list (frags_list, &file_template_frags_list);

	if (quiet)
		memset(&progress, 0, sizeof(progress));
	else
		progress_init(&progress, _("search through all blocks: "),
				blocks);
	
	for ( ; !set_block(0); )
	{
		progress_update(&progress, 
				file_template_frags_list->frag.fr_start);
		anysurrect_fromblock(info);
	}

	anysurrect_free_clean();
	free(block_bitmap);

	progress_close(&progress);

	r = write_it (info, (void*)inode_table);
	if (r)
	{
		fprintf(stderr,
				_("Error while writing inode table: %s\n"),
				(errno)?strerror(errno):_("format error"));
		goto free_out;
	}

free_out:
	free_it (info);

out:
	close(fd);
	
	for (lib=0; lib<num_libs; lib++)
	{
		if ( dlclose(libs_handles[lib]) )
		{
			fprintf( stderr, "Error dlclose(): %s\n",
					dlerror() );
			exit(1);
		}
	}

	return r;
}

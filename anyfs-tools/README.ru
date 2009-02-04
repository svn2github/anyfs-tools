anyfs-tools -- unix-way ����� ������������ ��� �������������� � �����������
�������� ������ ��� �� Linux.

��� ��������� ���������� �������:
$ ./configure
$ make
# make install

��� ���� '#' ������, ��� ������� ������� ���������� �����������������.

���� �� ����������� ���-���� ������� � �������� �����, �� �������� �����:
$ make depend

��� ���� ��� ������ ��� ����������� ����������:
 * e2fsprogs ������ 1.38 (�������� ���������� 
		 libext2fs, libe2p, libuuid, libblkid)
 * xfsprogs ������ 2.8.11 (�������� ���������� libxfs, libdisk)
		(�����������, ��� ��������������� � XFS)
 * libbz2 (�����������, ��� �������������� BZIP2 ������)
 * libmpeg2 (�����������, ��� �������������� MPEG2 stream)

���� � ��� ����� ������ ������ e2fsprogs ��� xfsprogs �� ������ ����������
���������, ���� ����� �����, � ����� �� ����������, �� ������ (��. ����).

�������� ����� ������� ��� ��������� ������� ��������� ��������� � ������:
anyfs-tools/src/anysurrect/surrectlist.conf
anyfs-tools/src/anysurrect/config.h

��� ����� ������������ ���� �� ���� (��� ��� ������ �����):
1) ��������� ���� (����� kernel-source) ��� ������ ������ ���� anyfs.
2) FUSE ������ >= 2.5.0 ��� ������ ������ anyfuse.

������ ������ ���� anyfs �������������� �� ����� 2.6.9-2.6.28.
��� ��������� ������ ����� ���������� � ����
/lib/modules/<������-����>/kernel/fs/any/any.ko

����� ��������� ������ ������� �������� ������������ �
$ man anyfs-tools

����� ���������� �������

1. ������: �� ������������ ������� man'�.
   �����: ������������� ���������� ��������� �������� � UTF-8 man'���
   	� ��������� �������������, ��� ����� Gentoo.
	� RedHat nroff ����Σ� ����������� �������� 
	"GNU nroff (groff) with Red Hat i18n/l10n support",
	��� � Redhat/Fedora-���������� ������������� ��� man'� �������� �
	UTF-8.
	
	� ������ ��� ��������� -- ����� man'� � UTF-8 ����� ����� �������������
	�� �� ����� ����� (������ ������������ �� ������� � ����������) 
	� �� ��������.

	� ��������� www.linuxshare.ru �������������� ���� man'�	� KOI8-R.
	������������� ������������ Gentoo ��� ���� ����������� ������� ���
	����� ��� KOI8-R man'� ��������. ���� ��, ������� �����, ���� �� ���
	�������������� �� �������������� � UTF-8. ����� ��� ���ģ���
	�������������� man'� anyfs-tools � KOI8-R.

� ��������� ������
��������� �� ������ undefer@gmail.com

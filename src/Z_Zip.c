/* Z_Zone.c */

//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//      Zone Memory Allocation. Neat.
//
// Based on the doom64 Ex code by Samuel Villarreal
// https://github.com/svkaiser/Doom64EX/blob/master/src/engine/zone/z_zone.cc
//-----------------------------------------------------------------------------

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <zlib.h>

#include "DoomRPG.h"
#include "Z_Zip.h"

zip_file_t zipFile;

static void* zip_alloc(void* ctx, unsigned int items, unsigned int size)
{
	return SDL_malloc(items * size);
}

static void zip_free(void* ctx, void* ptr)
{
	SDL_free(ptr);
}

static int16_t _ReadShort(FILE* fp)
{
	int16_t sData = 0;

	if (fp) {
		fread(&sData, sizeof(int16_t), 1, fp);
	}

	return sData;
}

static int32_t _ReadInt(FILE* fp)
{
	int32_t iData = 0;

	if (fp) {
		fread(&iData, sizeof(int32_t), 1, fp);
	}

	return iData;
}

void findAndReadZipDir(zip_file_t* zipFile, int startoffset)
{
	int sig, offset, count;
	int namesize, metasize, commentsize;
	int i;

	fseek(zipFile->file, startoffset, SEEK_SET);

	sig = _ReadInt(zipFile->file);
	if (sig != ZIP_END_OF_CENTRAL_DIRECTORY_SIG) {
		DoomRPG_Error("wrong zip end of central directory signature (0x%x)", sig);
	}

	printf("%d\n", _ReadShort(zipFile->file)); // this disk
	printf("%d\n", _ReadShort(zipFile->file)); // start disk
	printf("%d\n", _ReadShort(zipFile->file)); // entries in this disk
	printf("%d\n", count = _ReadShort(zipFile->file)); // entries in central directory disk
	printf("%d\n", _ReadInt(zipFile->file)); // size of central directory
	printf("%d\n", offset = _ReadInt(zipFile->file)); // offset to central directory
	
	if(count <= 0)
	{
		DoomRPG_Error("No entries in central directory disk.");
	}

	zipFile->entry = (zip_entry_t*)calloc(count, sizeof(zip_entry_t));
	zipFile->entry_count = count;

	fseek(zipFile->file, offset, SEEK_SET);

	for (i = 0; i < count; i++)
	{
		zip_entry_t* entry = zipFile->entry + i;

		sig = _ReadInt(zipFile->file);
		if (sig != ZIP_CENTRAL_DIRECTORY_SIG) {
			DoomRPG_Error("wrong zip central directory signature (0x%x)", sig);
		}

		_ReadShort(zipFile->file); // version made by
		_ReadShort(zipFile->file); // version to extract
		_ReadShort(zipFile->file); // general
		_ReadShort(zipFile->file); // method
		_ReadShort(zipFile->file); // last mod file time
		_ReadShort(zipFile->file); // last mod file date
		_ReadInt(zipFile->file); // crc-32
		entry->csize = _ReadInt(zipFile->file); // csize
		entry->usize = _ReadInt(zipFile->file); // usize
		namesize = _ReadShort(zipFile->file); // namesize
		metasize = _ReadShort(zipFile->file); // metasize
		commentsize = _ReadShort(zipFile->file); // commentsize
		_ReadShort(zipFile->file); // disk number start
		_ReadShort(zipFile->file); // int file atts
		_ReadInt(zipFile->file); // ext file atts
		entry->offset = _ReadInt(zipFile->file); // offset

		entry->name = (char*)malloc(namesize + 1);
		fread(entry->name, sizeof(char), namesize, zipFile->file);
		entry->name[namesize] = 0;

		fseek(zipFile->file, metasize, SEEK_CUR);
		fseek(zipFile->file, commentsize, SEEK_CUR);
	}
}

void openZipFile(const char* name, zip_file_t* zipFile)
{
	uint8_t buf[512];
	int filesize, back, maxback;
	int i, n;

	zipFile->file = fopen(name, "rb");
	if (zipFile->file == NULL) {
		DoomRPG_Error("openZipFile: cannot open file %s\n", name);
	}

	fseek(zipFile->file, 0, SEEK_END);
	filesize = (int)ftell(zipFile->file);
	fseek(zipFile->file, 0, SEEK_SET);

	maxback = MIN(filesize, 0xFFFF + sizeof(buf));
	back = MIN(maxback, sizeof(buf));

	while (back < maxback)
	{
		fseek(zipFile->file, filesize - back, SEEK_SET);
		n = sizeof(buf);
		fread(buf, sizeof(uint8_t), n, zipFile->file);
		for (i = n - 4; i > 0; i--)
		{
			if (!memcmp(buf + i, "PK\5\6", 4)) {
				findAndReadZipDir(zipFile, filesize - back + i);
				return;
			}
		}
		back += sizeof(buf) - 4;
	}

	DoomRPG_Error("cannot find end of central directory\n");
}

void closeZipFile(zip_file_t* zipFile)
{
	if (zipFile) {
		if (zipFile->entry) {
			free(zipFile->entry);
			zipFile->entry = NULL;
		}

		if (zipFile->file) {
			fclose(zipFile->file);
			zipFile->file = NULL;
		}
	}
}

unsigned char* readZipFileEntry(const char* name, zip_file_t* zipFile, int* sizep)
{
	zip_entry_t* entry = NULL;
	int i, sig, general, method, namelength, extralength;
	uint8_t* cdata;
	int code;

	for (i = 0; i < zipFile->entry_count; i++)
	{
		zip_entry_t* entryTmp = zipFile->entry + i;
		if (!SDL_strcasecmp(name, entryTmp->name)) {
			entry = zipFile->entry + i;
			break;
		}
	}

	if (entry == NULL) {
		DoomRPG_Error("did not find the %s file in the zip file", name);
	}

	fseek(zipFile->file, entry->offset, SEEK_SET);

	sig = _ReadInt(zipFile->file);
	if (sig != ZIP_LOCAL_FILE_SIG) {
		DoomRPG_Error("wrong zip local file signature (0x%x)", sig);
	}

	_ReadShort(zipFile->file); // version
	general = _ReadShort(zipFile->file); // general
	if (general & ZIP_ENCRYPTED_FLAG) {
		DoomRPG_Error("zipfile content is encrypted");
	}

	method = _ReadShort(zipFile->file); // method
	_ReadShort(zipFile->file); // file time
	_ReadShort(zipFile->file); // file date
	_ReadInt(zipFile->file); // crc-32
	_ReadInt(zipFile->file); // csize
	_ReadInt(zipFile->file); // usize
	namelength = _ReadShort(zipFile->file); // namelength
	extralength = _ReadShort(zipFile->file); // extralength

	fseek(zipFile->file, namelength + extralength, SEEK_CUR);

	cdata = (uint8_t*) malloc(entry->csize);
	fread(cdata, sizeof(uint8_t), entry->csize, zipFile->file);

	if (method == 0)
	{
		*sizep = entry->usize;
		return cdata;
	}
	else if (method == 8)
	{
		uint8_t* udata = (uint8_t*) malloc(entry->usize);
		z_stream stream;

		memset(&stream, 0, sizeof stream);
		stream.zalloc = zip_alloc;
		stream.zfree = zip_free;
		stream.opaque = Z_NULL;
		stream.next_in = cdata;
		stream.avail_in = entry->csize;
		stream.next_out = udata;
		stream.avail_out = entry->usize;

		code = inflateInit2(&stream, -15);
		if (code != Z_OK) {
			DoomRPG_Error("zlib inflateInit2 error: %s", stream.msg);
		}

		code = inflate(&stream, Z_FINISH);
		if (code != Z_STREAM_END) {
			inflateEnd(&stream);
			DoomRPG_Error("zlib inflate error: %s", stream.msg);
		}

		code = inflateEnd(&stream);
		if (code != Z_OK) {
			inflateEnd(&stream);
			DoomRPG_Error("zlib inflateEnd error: %s", stream.msg);
		}

		free(cdata);

		*sizep = entry->usize;
		return udata;
	}
	else {
		DoomRPG_Error("unknown zip method: %d", method);
	}

	return NULL;
}

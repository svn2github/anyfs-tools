/*
 *	audio_files_descr.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

char *audio_MIDI_surrect();
char *audio_WAV_surrect();
char *audio_MP3_surrect();

_declspec(dllexport) 
const char* audio_MIDI_indicator="MIDI";
_declspec(dllexport) 
const char* audio_WAV_indicator="WAV";
_declspec(dllexport) 
const char* audio_MP3_indicator="MP3";

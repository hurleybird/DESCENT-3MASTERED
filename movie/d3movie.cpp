/* 
* Descent 3 
* Copyright (C) 2024 Parallax Software
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdlib.h>

#ifdef __LINUX__
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>

#define O_BINARY 0
#endif

#include "DDAccess.h"

#ifdef WIN32
#include <MMSystem.h>
#include <io.h>
#endif


#include <fcntl.h>
#include <string.h>


#include "movie.h"
#include "pserror.h"
#include "renderer.h"
#include "application.h"
#include "ddio.h"
#include "grtext.h"
#include "mem.h"
#include "bitmap.h"
#include "gamefont.h"
#include "game.h"

#include "../mve/libmve.h"
#include "llsopenal.h"

//#define NO_MOVIES

namespace 
{
	MovieFrameCallback_fp Movie_callback = NULL;	
	char                  MovieDir[512];
	char                  SoundCardName[512];
	ushort                CurrentPalette[256];
	int                   Movie_bm_handle = -1;
	uint                  Movie_current_framenum = 0;
	bool                  Movie_looping = false;
	bool				  Movie_vid_set = false;
}

void* CallbackAlloc( unsigned int size );
void CallbackFree( void *p );
unsigned int CallbackFileRead( int hFile, void *pBuffer, unsigned int bufferCount );
void InitializePalette();
void CallbackSetPalette( unsigned char *pBuffer, unsigned int start, unsigned int count );
void CallbackShowFrame( unsigned char* buf, unsigned int bufw, unsigned int bufh,
						unsigned int sx, unsigned int sy, unsigned int w, unsigned int h,
						unsigned int dstx, unsigned int dsty, unsigned int hicolor );
void CallbackSequenceFrame( unsigned char* buf, unsigned int bufw, unsigned int bufh,
						unsigned int sx, unsigned int sy, unsigned int w, unsigned int h,
						unsigned int dstx, unsigned int dsty, unsigned int hicolor );


// sets the directory where movies are stored
int mve_Init( const char *dir, const char *sndcard )
{
#ifndef NO_MOVIES
	strcpy( MovieDir, dir );
	strcpy( SoundCardName, sndcard );	
	return MVELIB_NOERROR;
#else
	return MVELIB_INIT_ERROR;
#endif
}

// callback called per frame of playback of movies.
void mve_SetCallback( MovieFrameCallback_fp callBack )
{
#ifndef NO_MOVIES
	Movie_callback = callBack;
#endif
}

llsSystem* mve_soundSystem;
void mve_SetSoundSystem(llsSystem* system)
{
	mve_soundSystem = system;
}

// used to tell movie library how to render movies.
void mve_SetRenderProperties( short x, short y, short w, short h, renderer_type type, bool hicolor )
{
}

// plays a movie using the current screen.
int mve_PlayMovie( const char *pMovieName, oeApplication *pApp )
{
#ifndef NO_MOVIES
	// open movie file.
	int hFile = open( pMovieName, O_RDONLY|O_BINARY );
	if( hFile == -1 )
	{
		mprintf(( 0, "MOVIE: Unable to open %s\n", pMovieName ));
		return MVELIB_FILE_ERROR;
	}

	// determine the movie type
	const char* pExtension = strrchr( pMovieName, '.' );
	bool highColor = ( pExtension != NULL && stricmp( pExtension, ".mv8" ) != 0 );

	// setup
	//MVE_rmFastMode( MVE_RM_NORMAL );
	MVE_sfCallbacks( CallbackShowFrame );
	MVE_memCallbacks( CallbackAlloc, CallbackFree );
	MVE_ioCallbacks( CallbackFileRead );
	//MVE_sfSVGA( 640, 480, 480, 0, NULL, 0, 0, NULL, highColor ? 1 : 0 );
	MVE_palCallbacks( CallbackSetPalette );
	MVE_rmSetNoAudio(0);
	MVE_rmSetNoWait(0);
	InitializePalette();
	Movie_bm_handle = -1;

	int result = MVE_rmPrepMovie( hFile, -1, -1, 0 );
	if( result != 0 )
	{
		mprintf(( 0, "PrepMovie result = %d\n", result ));
		close( hFile );
		return MVELIB_INIT_ERROR;
	}

	renderer_preferred_state oldstate = Render_preferred_state;
	Movie_vid_set = false;

	bool aborted = false;
	Movie_current_framenum = 0;
	while( (result = MVE_rmStepMovie()) == 0 )
	{
		// let the OS do its thing
		pApp->defer();
	
		// check for bail
		int key = ddio_KeyInKey();
		if (IsAltEnterFullscreenEnabled() && IsAltEnterFullscreenKey(key))
		{
			ToggleFullscreenMode();
			oldstate = Render_preferred_state;
			Movie_vid_set = false;
			continue;
		}
		if (key == KEY_ESC)
		{
			aborted = true;
			break;
		}
	}

	// free our bitmap
	if( Movie_bm_handle != -1 )
	{
		bm_FreeBitmap( Movie_bm_handle );
		Movie_bm_handle = -1;
	}

	// close our file handle
	close( hFile );

	// determine the return code
	int err = MVELIB_NOERROR;
	if( aborted )
	{
		err = MVELIB_PLAYBACK_ABORTED;
	}
	else if( result != MVE_ERR_EOF )
	{
		err = MVELIB_PLAYBACK_ERROR;
	}

	// cleanup and shutdown
	MVE_rmEndMovie();
	MVE_ReleaseMem();
	mve_SetSoundSystem(NULL);

	if (Movie_vid_set)
	{
		rend_SetPreferredState(&oldstate);
		Render_preferred_state = oldstate;
	}

	// return out
	return err;
#else
	return MVELIB_INIT_ERROR;
#endif
}

void* CallbackAlloc( unsigned int size )
{
	return mem_malloc( size );
}

void CallbackFree( void *p )
{
	mem_free( p );
}

unsigned int CallbackFileRead( int hFile, void *pBuffer, unsigned int bufferCount )
{
    unsigned int numRead = read( hFile, pBuffer, bufferCount );
	return ( numRead == bufferCount ) ? 1 : 0;
}

void InitializePalette()
{
	for( int i = 0; i < 256; ++i )
	{
		CurrentPalette[i] = OPAQUE_FLAG | GR_RGB16(0,0,0);
	}
}

void CallbackSetPalette( unsigned char *pBuffer, unsigned int start, unsigned int count )
{
#ifndef NO_MOVIES
	pBuffer += start * 3;

	for( unsigned int i = 0; i < count; ++i )
	{
		unsigned int r = pBuffer[ 0 ] << 2;
		unsigned int g = pBuffer[ 1 ] << 2;
		unsigned int b = pBuffer[ 2 ] << 2;
		pBuffer += 3;

		CurrentPalette[ start + i ] = OPAQUE_FLAG | GR_RGB16( r, g, b );
	}
#endif
}

int NextPow2( int n )
{
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
	return n;
}

#ifndef NO_MOVIES
void BlitToMovieBitmap(unsigned char* buf, unsigned int bufw, unsigned int bufh, unsigned int hicolor, bool usePow2Texture, int& texW, int& texH)
{
	// get some sizes
	int drawWidth  = hicolor ? (bufw >> 1) : bufw;
	int drawHeight = bufh;

	texW = drawWidth;
	texH = drawHeight;

	if( Movie_bm_handle == -1 )
	{
		// Allocate our bitmap
		Movie_bm_handle = bm_AllocBitmap( texW, texH, 0 );
	}

	unsigned short* pPixelData = (ushort *)bm_data( Movie_bm_handle, 0 );
	GameBitmaps[Movie_bm_handle].flags |= BF_CHANGED;
	if( hicolor )
	{
		unsigned short* wBuf = (unsigned short*)buf;
		for( int y = 0; y < drawHeight; ++y )
		{
			for( int x = 0; x < drawWidth; ++x )
			{
				unsigned short col16 = *wBuf++;
				//[ISB] The decoded colors are apparently RGB555, but this code uses RGB565. Does libmve translate it normally?
#if 1
				pPixelData[x] = col16 | OPAQUE_FLAG;
#else
				unsigned int b = (( col16 >> 11 ) & 0x1F) << 3;
				unsigned int g = (( col16 >> 5  ) & 0x3F) << 2;
				unsigned int r = (( col16 >> 0  ) & 0x1F) << 3;
				pPixelData[ x ] = OPAQUE_FLAG | GR_RGB16( r, g, b );
#endif
			}

			pPixelData += texW;
		}
	}
	else
	{
		for( int y = 0; y < drawHeight; ++y )
		{
			for( int x = 0; x < drawWidth; ++x )
			{
				unsigned char palIndex = *buf++;
				pPixelData[ x ] = CurrentPalette[ palIndex ];
			}

			pPixelData += texW;
		}
	}
}

void CallbackShowFrame( unsigned char* buf, unsigned int bufw, unsigned int bufh,
					    unsigned int sx, unsigned int sy, unsigned int w, unsigned int h,
						unsigned int dstx, unsigned int dsty, unsigned int hicolor )
{
	// prepare our bitmap
	int texW, texH;
	BlitToMovieBitmap( buf, bufw, bufh, hicolor, false, texW, texH );

	// calculate UVs from texture
	int drawWidth  = hicolor ? (bufw >> 1) : bufw;
	int drawHeight = bufh;
	float u = float(drawWidth-1) / float(texW-1);
	float v = float(drawHeight-1) / float(texH-1);

	if (!Movie_vid_set)
	{
		Render_preferred_state.width = drawWidth;
		Render_preferred_state.height = drawHeight;
		Movie_vid_set = true;

		rend_SetPreferredState(&Render_preferred_state);
	}

	StartFrame( 0, 0, drawWidth, drawHeight, false );

	rend_ClearScreen( GR_BLACK );
	rend_SetAlphaType( AT_CONSTANT );
	rend_SetAlphaValue( 255 );
	rend_SetLighting( LS_NONE );
	rend_SetColorModel( CM_MONO );
	rend_SetOverlayType( OT_NONE );
	rend_SetWrapType( WT_CLAMP );
	rend_SetFiltering( 0 );
	rend_SetZBufferState( 0 );
	rend_DrawScaledBitmap( 0, 0, drawWidth, drawHeight, Movie_bm_handle, 0.0f, 0.0f, u, v );
	rend_SetFiltering( 1 );
	rend_SetZBufferState( 1 );

	// call our callback
	if( Movie_callback != NULL )
	{
		Movie_callback( dstx, dsty, Movie_current_framenum );
	}
	++Movie_current_framenum;

	EndFrame();

	rend_Flip();
}

void CallbackSequenceFrame( unsigned char* buf, unsigned int bufw, unsigned int bufh,
					    unsigned int sx, unsigned int sy, unsigned int w, unsigned int h,
						unsigned int dstx, unsigned int dsty, unsigned int hicolor )
{
	int texW, texH;
	BlitToMovieBitmap( buf, bufw, bufh, hicolor, false, texW, texH );
	++Movie_current_framenum;
}
#endif

unsigned int mve_SequenceStart( const char *mvename, int *fhandle, oeApplication *app, bool looping )
{
#ifndef NO_MOVIES

	int hfile = open( mvename, O_RDONLY|O_BINARY );

	if (hfile ==  -1)
	{
		mprintf((1, "MOVIE: Unable to open %s\n", mvename));
		*fhandle = -1;
		return 0;
	}

	// setup
	//MVE_rmFastMode( MVE_RM_NORMAL );
	MVE_sfCallbacks( CallbackSequenceFrame );
	MVE_memCallbacks( CallbackAlloc, CallbackFree );
	MVE_ioCallbacks( CallbackFileRead );
	MVE_palCallbacks( CallbackSetPalette );
	MVE_rmSetNoAudio(1);
	MVE_rmSetNoWait(1);
	InitializePalette();
	Movie_bm_handle = -1;
	Movie_looping   = looping;
	Movie_current_framenum = 0;

	// let the render know we will be copying bitmaps to framebuffer (or something)
	rend_SetFrameBufferCopyState(true);

	*fhandle = hfile;
	if (MVE_rmPrepMovie(hfile, -1, -1, 0) != 0)
	{
		close(hfile);
		*fhandle = -1;
		return 0;
	}

	return 1;
#else
	return 0;
#endif
}

unsigned int mve_SequenceFrame( unsigned int handle, int fhandle, bool sequence, int *bm_handle )
{
#ifndef NO_MOVIES
	if( bm_handle )
	{
		*bm_handle = -1;
	}

	if( handle == -1 )
	{
		return (unsigned int)(-1);
	}

	int err            = 0;

reread_frame:
	if( bm_handle )
	{
		*bm_handle = -1;
	}

	err = MVE_rmStepMovie();
	if( err == 0 )
	{
		if( bm_handle )
		{
			*bm_handle = Movie_bm_handle;
		}

		return handle;
	}

	if( Movie_looping && err == MVE_ERR_EOF )
	{
		if (MVE_rmPrepMovie(fhandle, -1, -1, 0) != 0)
			return (unsigned int)( -1 );
		sequence = true;
		goto reread_frame;
	}

	return (unsigned int)( -1 );
#else
	return (unsigned int)(-1);
#endif
}

bool mve_SequenceClose( unsigned int hMovie, int hFile )
{
#ifndef NO_MOVIES
	if( hMovie == -1 )
		return false;

	MVE_rmEndMovie();
	MVE_ReleaseMem();
	close( hFile );

	// free our bitmap
	if( Movie_bm_handle != -1 )
	{
		bm_FreeBitmap( Movie_bm_handle );
		Movie_bm_handle = -1;
	}

	// We're no longer needing this
	rend_SetFrameBufferCopyState( false );

	return true;
#else
	return false;
#endif
}

void mve_Puts( short x, short y, ddgr_color col, const char *txt )
{
	grtext_SetFont( BRIEFING_FONT );
	grtext_SetColor( col );
	grtext_SetAlpha( 255 );
	grtext_SetFlags( GRTEXTFLAG_SHADOW );
	grtext_CenteredPrintf( 0, y, txt );
	grtext_Flush();
}

void mve_ClearRect( short x1, short y1, short x2, short y2 )
{
	//Note: I can not figure out how to clear, and then write over it with text. It always covers my text!
	//rend_FillRect( GR_BLACK, x1, y1, x2, y2 );
}

/**********************************************************
 Software developed by Hessel van der Molen
 Main author Hessel van der Molen (hmolen.science at gmail dot com)
 This software is released under BSD license as expressed below
 -------------------------------------------------------------------
 Copyright (c) 2017, Hessel van der Molen
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 1. Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 3. All advertising materials mentioning features or use of this software
 must display the following acknowledgement:
 
 This product includes software developed by Hessel van der Molen
 
 4. None of the names of the author or irs contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY Hessel van der Molen ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL AVA BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****************************************************************/

//
// Functions to adjust terminal/shell behaviour
//

#ifndef FlashCam_util_terminal_h
#define FlashCam_util_terminal_h

#include <stdio.h>

//https://en.wikipedia.org/wiki/ANSI_escape_code
#define GOTO(a,b) printf("\033[%d;%dH",a,b)
#define NL        printf("\033[K\r\n")

#define CLEARLINE printf("\033[2K")
#define HIDECURS  printf("\033[?25l")
#define SHOWCURS  printf("\033[?25h")

#define CLEAR     printf("\033[2J")
#define TPOS(a,b) "\033[" #a ";" #b "H"
#define TCNL      "\033[K\n"
#define TCL       "\033[K"

/*
 #define CLEAR     
 #define TPOS(a,b) ""
 #define TCNL      "\n"
 #define TCL       "\n"
 */


void terminal_clear( void );
void terminal_restore( void );

void terminal_setEcho( int fd, int enable );
void terminal_setBlocking( int fd, int block );
void terminal_enableBuffer( int fd, int enable );



#endif /* FlashCam_util_terminal_h */

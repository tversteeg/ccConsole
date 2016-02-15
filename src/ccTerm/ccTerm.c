#include <ccTerm/ccTerm.h>

#include <stdio.h>
#include <string.h>

#define _CCT_PIXEL_FORMAT GL_RGBA
#define _CCT_PIXEL_TYPE GL_UNSIGNED_BYTE

typedef struct {
	unsigned char r, g, b, a;
} _cctPixel;

static void _cctCalcWidth(cctTerm *term)
{
	if(term->font == NULL){
		return;
	}
	term->outwidth = (term->width - 4) / term->font->gwidth;
	term->outheight = ((term->height - 4) / term->font->gheight - 1);
}

static void _cctSetSize(cctTerm *term, unsigned width, unsigned height)
{
	if(term->pixels){
		free(term->pixels);
		term->pixels = NULL;
	}
	term->pixels = (void*)malloc(width * height * sizeof(_cctPixel));

	term->width = width;
	term->height = height;

	_cctCalcWidth(term);
}

static void _cctRenderIn(cctTerm *term)
{	
	ccfFontConfiguration conf = {
		.x = 2, 
		.y = term->height - (term->font->gheight + 2), 
		.width = term->width, 
		.wraptype = 0, 
		.color = {1.0, 1.0, 1.0, 1.0}
	};

	ccfGLTexBlitChar(term->font, '>', &conf, term->width, term->height, _CCT_PIXEL_FORMAT, _CCT_PIXEL_TYPE, term->pixels);

	ccfGLTexBlitText(term->font, term->in, &conf, term->width, term->height, _CCT_PIXEL_FORMAT, _CCT_PIXEL_TYPE, term->pixels);
}

static void _cctRenderOut(cctTerm *term)
{
	//TODO calculate total height for scrolling
	size_t len = strlen(term->out);
	if(len == 0){
		return;
	}

	ccfFontConfiguration conf = {
		.width = term->width, 
		.wraptype = 0, 
		.color = {1.0, 1.0, 1.0}
	};

	unsigned x = 0;
	unsigned y = 0;
	for(unsigned i = 0; i < len; i++){
		conf.x = x * term->font->gwidth + 2;
		conf.y = y * term->font->gheight + 2;

		char c = term->out[i];
		if(c == '\n'){
			x = 0;
			y++;
			continue;
		}else if(c == '\t'){
			x += (4 - x % 4);
		}else if(c == '\a'){
			//TODO parse color and set font color
			char colc;
			do{
				colc = term->out[++i];
			}while(colc != 'm' && colc);

			x--;
		}else if(c != ' '){
			ccfGLTexBlitChar(term->font, term->out[i], &conf, term->width, term->height, _CCT_PIXEL_FORMAT, _CCT_PIXEL_TYPE, term->pixels);
		}

		x++;
		if(x == term->outwidth){
			x = 0;
			y++;
		}
	}
}

static void _cctRenderBlink(cctTerm *term)
{
	if(term->blink > CCT_BLINK_INTERVAL){
		term->blink = -CCT_BLINK_INTERVAL;
	}
	term->blink++;

	if(term->blink > 0){
		return;
	}

	unsigned startx = 2 + (term->inpos + 1) * term->font->gwidth;
	unsigned starty = term->height - (term->font->gheight + 2);

	for(int y = 0; y < term->font->gheight; y++){
		for(int x = 0; x < term->font->gwidth; x++){
			int i = x + startx + (y + starty) * term->width;
			unsigned char *pix = term->pixels + i * sizeof(_cctPixel);
			pix[0] = pix[1] = pix[2] = pix[3] = 128;
		}
	}
}

static void __cctPrintf(cctTerm *term, const char *text)
{
	size_t len = strlen(text);

	size_t total = term->outlen + len;
	while(total > term->outmaxlen){
		term->outmaxlen *= 2;

		//TODO make safe
		term->out = (char*)realloc(term->out, term->outmaxlen);
	}

	strcpy(term->out + term->outlen, text);
	term->outlen = total;
}

static int _cctSplitStr(const char *str, char ***res)
{
	int count = 0;
	int max = 10;

	*res = (char**)malloc(max * sizeof(char*));

	const char *e = str;

	if(e) do{
		const char *s = e;
		e = strpbrk(s, " ");

		if(count > max){
			max *= 2;
			*res = (char**)realloc(*res, max * sizeof(char*));
		}

		if(e){
			(*res)[count++] = strndup(s, e - s);
		}else{
			(*res)[count++] = strdup(s);
		}
	}while(e && *(++e));
	
	if(count > max){
		*res = (char**)realloc(*res, ++max * sizeof(char*));
	}

	(*res)[count + 1] = 0;

	return count;
}

static void _cctParseInput(cctTerm *term)
{
	char **argv = NULL;
	int argc = _cctSplitStr(term->in, &argv);
	size_t namelen = strlen(argv[0]);

	int command = -1;
	for(int i = 0; i < term->ncmds; i++){
		if(strncmp(term->cmds[i], argv[0], namelen) == 0){
			command = i;
		}
	}

	if(command < 0){
		cctPrintf(term, "Command \"%s\" not found!\n", argv[0]);
	}else{
		term->cmdfuncs[command](term, argc, argv);
	}

	free(argv);
}

void cctCreate(cctTerm *term, unsigned width, unsigned height)
{
	term->font = NULL;

	term->outmaxlen = 16;
	term->out = (char*)calloc(term->outmaxlen, 1);
	term->outlen = 0;

	term->insert = false;

	term->ncmds = 0;

	memset(term->in, 0, CCT_MAX_CMD_LEN);

	_cctSetSize(term, width, height);
}

void cctFree(cctTerm *term)
{
	free(term->out);
}

void cctResize(cctTerm *term, unsigned width, unsigned height)
{
	_cctSetSize(term, width, height);
}

void cctSetFont(cctTerm *term, ccfFont *font)
{
	term->font = font;

	_cctCalcWidth(term);
}

void cctSetInsert(cctTerm *term, bool insert)
{
	term->insert = insert;
}

void cctRender(cctTerm *term, GLuint texture)
{
	memset(term->pixels, 0, term->width * term->height * sizeof(_cctPixel));

	_cctRenderOut(term);
	_cctRenderIn(term);
	_cctRenderBlink(term);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, term->width, term->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, term->pixels);

	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(-1.0f, 1.0f);
	glTexCoord2f(0.0f, 1.0f);
	glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(1.0f, -1.0f);
	glTexCoord2f(1.0f, 0.0f);
	glVertex2f(1.0f, 1.0f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, 0);
}

void cctHandleEvent(cctTerm *term, ccEvent event)
{
	if(event.type != CC_EVENT_KEY_DOWN){
		return;
	}

	if(event.keyCode == CC_KEY_LEFT){
		if(term->inpos > 0){
			term->inpos--;
		}
	}else if(event.keyCode == CC_KEY_RIGHT){
		if(term->in[term->inpos] != '\0'){
			term->inpos++;
		}
	}else if(event.keyCode == CC_KEY_BACKSPACE){
		if(term->inpos > 0){
			term->inpos--;
			memmove(term->in + term->inpos, term->in + term->inpos + 1, strlen(term->in) - term->inpos);
		}
	}else if(event.keyCode == CC_KEY_RETURN){
		_cctParseInput(term);
		term->inpos = 0;
		term->in[0] = '\0';
	}else{
		char c = ccEventKeyToChar(event.keyCode);
		if(c != 0){
			if(term->insert){
				term->in[term->inpos] = c;
				if(++term->inpos == strlen(term->in) - 1){
					term->in[term->inpos] = '\0';
				}
			}else{
				size_t len = strlen(term->in);
				if(term->inpos == len){
					term->in[term->inpos] = c;
					term->in[++term->inpos] = '\0';
				}else if(len < CCT_MAX_CMD_LEN){
					memmove(term->in + term->inpos + 1, term->in + term->inpos, len - term->inpos);
					term->in[++term->inpos] = c;
				}
			}
		}
	}
}

void _cctPrintf(cctTerm *term, const char *text, ...)
{
	va_list arg;

	va_start(arg, text);
	char buf[256];
	vsprintf(buf, text, arg);
#ifdef CCT_MIRROR_STDOUT
	printf(buf);
#endif
	__cctPrintf(term, buf);
	va_end(arg);
}

void cctMapCmd(cctTerm *term, const char *cmd, cctCmdptr cmdfunc)
{
	term->cmdfuncs[term->ncmds] = cmdfunc;
	term->cmds[term->ncmds] = (char*)malloc(strlen(cmd));
	strcpy(term->cmds[term->ncmds], cmd);
	term->ncmds++;
}

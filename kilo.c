/*** includes ***/
#define _DEFAULT_SOURCE  /* As três defines a seguir são para que o compilador não reclame do getline */
#define _BSD_SOURCE /*  Os defines estão acima dos includes, pois os includes utilizados usam macros para decidir qual recurso expor */
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)


enum editorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/*** data ***/

typedef struct erow {
	int size;
	char *chars;
} erow;

struct editorConfig {
	int cx, cy;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	struct termios orig_termios; /* Variável que guardará os atributos originais do terminal */
};

struct editorConfig E;
/*** terminal ***/

void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	
	perror(s); /* A maioria das fç do std que falham definirá uma varíavel global errno, perror avalia essa variável e imprime uma msg para ela */
	exit(1);
	/*
	 * perror - stdio.h
	 * exit - stdlib.h
	*/
}
void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}
void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode); /* Função interessante do stlib.h, "NA SAIDA", pois quebra implicitamente o fluxo do programa */
	
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/*
	 * IXON - desabilita os sinais do CtrlS (impede os dados para o terminal) e CtrlQ (desfaz o CtrlS)
	 * ICRNL - desabilita o CrtlM e CtrlJ, tendo em vista que Cr - carriage return 'r' e Nl - '\n'
	 * Os demais sinalizadores não possuem um efeito observável
	*/
	raw.c_oflag &= ~(OPOST);
	/*
	 * OPOST - desativa a saída padrão do new line que é '\r\n' para apenas '\n'
	*/
	
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG); 
	/*
	 * ECHO - impressão de teclas pressionadas na tela,
	 * ICANON - desliga a leitura de frase por frase, e, lemos por caracter
	 * ISIG - Desliga os sinais SIGINT do CtrlD e SIGSTP do CtrlZ
	*/ 
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	/*
	 * são index do control characters
	 * O VMIN define o número mínimo de bytes de entrada necessários antes que o read possa retornar
	 * Neste caso, o 0 significa assim que algum byte seja lido
	 * O VTIME define o tempo máximo para que o read() dê retorno, neste caso 0.1 segundos
	*/
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
	int nread;
	char c;
	while ( (nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(nread == -1 && errno != EAGAIN) die("read");
	}
	
	if (c == '\x1b') {
		char seq[3];
		
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
		
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'F': return END_KEY;
					case 'H': return HOME_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch(seq[1]) {
				case 'F': return END_KEY;
				case 'H': return HOME_KEY;
			}
		}
		
		return '\x1b';
	} else {
		return c;
	}	
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws; /* esta struct, como o ioctl e TIOCGWINSZ origina de sys/ioctl.h */
	
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		return -1;
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}
/*** row operations ***/

void editorAppendRow(char *s, size_t len) {
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	
	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;
}

/*** file i/o ***/

void editorOpen(char *filename) {
	FILE *fp = fopen(filename, "r"); /* FILE, fopen e getline vem de stdio.h */
	if(!fp) die("fopen");
	
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen; /* ssize_t oriunda de sys/types.h */
	
	while ((linelen = getline(&line, &linecap, fp)) != -1) {	
		while (linelen  > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--; /* Remove \n e \r do final da linha */
		editorAppendRow(line, linelen);
	}
	
	free(line);
	fclose(fp);
	
}

/*** append buffer ***/

struct abuf {
	char *b;
	int len;
};


#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);
	
	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}
/*** output ***/

void editorScroll() {
	if (E.cy < E.rowoff) E.rowoff = E.cy;
	if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
	
	if (E.cx < E.coloff) E.coloff = E.cx;
	if (E.cx >= E.coloff + E.screencols) E.coloff = E.cx - E.screencols + 1;
}

void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), 
					"Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > E.screencols) welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if(padding) {
					abAppend(ab, "~", 1);
					padding--;
				}
				while(padding--) abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1);
			}
		} else {
			int len = E.row[filerow].size - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].chars[E.coloff], len);
		}
		
		abAppend(ab, "\x1b[K", 3);		
		if (y < E.screenrows -1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
		editorScroll();
	
		struct abuf ab = ABUF_INIT;
		
		abAppend(&ab, "\x1b[?25l", 6); /* Oculta o cursor */
		
		abAppend(&ab, "\x1b[H", 3); /*reposiciona o cursor na primeiras linha e coluna */
		
		editorDrawRows(&ab);
		
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
		abAppend(&ab, buf, strlen(buf));
		
		abAppend(&ab, "\x1b[?25h", 6); /* Exibe o cursor novamente */
		
		write(STDOUT_FILENO, ab.b, ab.len);
		abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) E.cx--;
			else if (	E.cy > 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size)  E.cx++;
			else if (row && E.cx == row->size) {
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) E.cy++;
			break;
	}
	
	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) E.cx = rowlen;

}

void editorProcessKeypress() {
	int c = editorReadKey();
	
	switch(c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		
		case HOME_KEY:
			E.cx = 0;
			break;
		
		case END_KEY:
			E.cx = E.screencols - 1;
			break;
			
		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = E.screenrows;
				while(times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN); 
			}
			break;
			
		case ARROW_LEFT:
		case ARROW_RIGHT:
		case ARROW_UP:
		case ARROW_DOWN:
			editorMoveCursor(c);
			break;
	}
}
/*** init ***/

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	
	if (argc >= 2) {
		editorOpen(argv[1]);
	}
	
	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	
	return 0;
}

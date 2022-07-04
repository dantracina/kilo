/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)


enum editorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN
};

/*** data ***/

struct editorConfig {
	int cx, cy;
	int screenrows;
	int screencols;
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
			switch (seq[1]) {
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
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

void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		if (y == E.screenrows / 3) {
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
		
		abAppend(ab, "\x1b[K", 3);		
		if (y < E.screenrows -1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
		struct abuf ab = ABUF_INIT;
		
		abAppend(&ab, "\x1b[?25l", 6); /* Oculta o cursor */
		
		abAppend(&ab, "\x1b[H", 3); /*reposiciona o cursor na primeiras linha e coluna */
		
		editorDrawRows(&ab);
		
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
		abAppend(&ab, buf, strlen(buf));
		
		abAppend(&ab, "\x1b[?25h", 6); /* Exibe o cursor novamente */
		
		write(STDOUT_FILENO, ab.b, ab.len);
		abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) E.cx--;
			break;
		case ARROW_RIGHT:
			if (E.cx != E.screencols - 1) E.cx++;
			break;
		case ARROW_UP:
			if (E.cy != 0) E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy != E.screenrows - 1) E.cy++;
			break;
	}

}

void editorProcessKeypress() {
	int c = editorReadKey();
	
	switch(c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
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
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
	enableRawMode();
		initEditor();
		
	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	
	return 0;
}

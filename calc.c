#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void trim(char *s) {
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1])) {
		s[--n] = '\0';
	}
}

static void print_help(void) {
	printf("Enter: <a> <op> <b>  where op in + - * /\n");
	printf("Examples: 3 + 4\n         12.5 * 2\nType 'q' or 'quit' to exit.\n");
}

int main(void) {
	char line[256];
	print_help();
	for (;;) {
		printf("> ");
		if (!fgets(line, sizeof(line), stdin)) {
			break;
		}
		trim(line);
		if (line[0] == '\0') {
			continue;
		}
		if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) {
			break;
		}
		if (strcmp(line, "help") == 0) {
			print_help();
			continue;
		}

		double a, b;
		char op;
		char extra;
		if (sscanf(line, "%lf %c %lf %c", &a, &op, &b, &extra) == 4) {
			printf("Error: too many tokens. Try: 3 + 4\n");
			continue;
		}
		if (sscanf(line, "%lf %c %lf", &a, &op, &b) != 3) {
			printf("Parse error. Try 'help'.\n");
			continue;
		}

		double result = 0.0;
		int ok = 1;
		switch (op) {
			case '+':
				result = a + b;
				break;
			case '-':
				result = a - b;
				break;
			case '*':
				result = a * b;
				break;
			case 'x':
			case 'X':
				result = a * b;
				break;
			case '/':
				if (b == 0.0) {
					printf("Error: division by zero.\n");
					ok = 0;
				} else {
					result = a / b;
				}
				break;
			default:
				printf("Unknown op '%c'. Use + - * /\n", op);
				ok = 0;
				break;
		}

		if (ok) {
			printf("= %.10g\n", result);
		}
	}
	printf("Bye!\n");
	return 0;
}



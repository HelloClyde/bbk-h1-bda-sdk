#ifndef H1_TEXT_INPUT_H
#define H1_TEXT_INPUT_H
/* V1.41 modal GB2312 editor. UTF-8 buffer, GBK caption. Close game drawing
 * context/timer first, restore after return. 1 accepted, 0 cancel/unrepresentable. */
int h1_input_text(char *utf8,unsigned capacity,const char *caption_gbk);
#endif

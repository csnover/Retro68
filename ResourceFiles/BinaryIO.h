#ifndef BINARYIO_H
#define BINARYIO_H

#include <iosfwd>
#include <string>

class ResType;

inline void word(unsigned char *p, int value)
{
    p[0] = value >> 8;
    p[1] = value;
}

inline void longword(unsigned char *p, int value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

void byte(std::ostream& out, int byte);
void word(std::ostream& out, int word);
void ostype(std::ostream& out, ResType type);
void longword(std::ostream& out, int longword);

int byte(std::istream& in);
int word(std::istream& in);
ResType ostype(std::istream& in);
int longword(std::istream& in);

#endif // BINARYIO_H

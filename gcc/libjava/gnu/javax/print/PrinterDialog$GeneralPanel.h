
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_javax_print_PrinterDialog$GeneralPanel__
#define __gnu_javax_print_PrinterDialog$GeneralPanel__

#pragma interface

#include <javax/swing/JPanel.h>
extern "Java"
{
  namespace gnu
  {
    namespace javax
    {
      namespace print
      {
          class PrinterDialog;
          class PrinterDialog$GeneralPanel;
          class PrinterDialog$GeneralPanel$CopiesAndSorted;
          class PrinterDialog$GeneralPanel$PrintRange;
          class PrinterDialog$GeneralPanel$PrintServices;
      }
    }
  }
}

class gnu::javax::print::PrinterDialog$GeneralPanel : public ::javax::swing::JPanel
{

public:
  PrinterDialog$GeneralPanel(::gnu::javax::print::PrinterDialog *);
public: // actually package-private
  void update();
  static ::gnu::javax::print::PrinterDialog * access$0(::gnu::javax::print::PrinterDialog$GeneralPanel *);
private:
  ::gnu::javax::print::PrinterDialog$GeneralPanel$PrintServices * __attribute__((aligned(__alignof__( ::javax::swing::JPanel)))) printserv_panel;
  ::gnu::javax::print::PrinterDialog$GeneralPanel$PrintRange * printrange_panel;
  ::gnu::javax::print::PrinterDialog$GeneralPanel$CopiesAndSorted * copies;
public: // actually package-private
  ::gnu::javax::print::PrinterDialog * this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_javax_print_PrinterDialog$GeneralPanel__
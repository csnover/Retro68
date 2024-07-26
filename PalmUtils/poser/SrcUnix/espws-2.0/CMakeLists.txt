add_library(poserespws STATIC
    CheckButton.cxx
    CheckButton.h
    FileBrowser.cxx
    FileBrowser.h
    FileChooser.cxx
    FileChooser.fl
    FileChooser.h
    FileChooser2.cxx
    FileIcon.cxx
    FileIcon.h
    FileInput.cxx
    FileInput.h
    Fl_Wizard.cxx
    Fl_Wizard.h
    HelpApp.cxx
    HelpApp.fl
    HelpApp.h
    HelpApp2.cxx
    HelpDialog.cxx
    HelpDialog.fl
    HelpDialog.h
    HelpView.cxx
    HelpView.h
)

target_include_directories(poserespws INTERFACE .)

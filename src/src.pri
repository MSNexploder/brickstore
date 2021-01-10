RELPWD = $$replace(PWD,$$_PRO_FILE_PWD_,.)

INCLUDEPATH += $$RELPWD
DEPENDPATH  += $$RELPWD

QT *= core gui xml network # networkauth
!backend-only:QT *= widgets script scripttools printsupport uitools qml quick

win32:QT *= winextras widgets

OTHER_FILES += \
  $$PWD/version.h.in \

SOURCES += \
  $$PWD/main.cpp \
  $$PWD/rebuilddatabase.cpp \
  $$PWD/ref.cpp \

HEADERS += \
  $$PWD/rebuilddatabase.h \
  $$PWD/ref.h \

!backend-only {

SOURCES += \
  $$PWD/appearsinwidget.cpp \
  $$PWD/application.cpp \
  $$PWD/checkforupdates.cpp \
  $$PWD/config.cpp \
  $$PWD/document.cpp \
  $$PWD/documentdelegate.cpp \
  $$PWD/framework.cpp \
  $$PWD/import.cpp \
  $$PWD/itemdetailpopup.cpp \
  $$PWD/picturewidget.cpp \
  $$PWD/priceguidewidget.cpp \
  $$PWD/qml_bricklink_wrapper.cpp \
  $$PWD/report.cpp \
  $$PWD/reportobjects.cpp \
  $$PWD/selectcolor.cpp \
  $$PWD/selectitem.cpp \
  $$PWD/taskwidgets.cpp \
  $$PWD/updatedatabase.cpp \
  $$PWD/welcomewidget.cpp \
  $$PWD/window.cpp

HEADERS += \
  $$PWD/appearsinwidget.h \
  $$PWD/application.h \
  $$PWD/checkforupdates.h \
  $$PWD/config.h \
  $$PWD/document.h \
  $$PWD/document_p.h \
  $$PWD/documentdelegate.h \
  $$PWD/framework.h \
  $$PWD/import.h \
  $$PWD/itemdetailpopup.h \
  $$PWD/picturewidget.h \
  $$PWD/priceguidewidget.h \
  $$PWD/qml_bricklink_wrapper.h \
  $$PWD/report.h \
  $$PWD/report_p.h \
  $$PWD/reportobjects.h \
  $$PWD/selectcolor.h \
  $$PWD/selectitem.h \
  $$PWD/taskwidgets.h \
  $$PWD/updatedatabase.h \
  $$PWD/welcomewidget.h \
  $$PWD/window.h \

FORMS = \
  $$PWD/aboutdialog.ui \
  $$PWD/additemdialog.ui \
  $$PWD/changecurrencydialog.ui \
  $$PWD/consolidateitemsdialog.ui \
  $$PWD/importinventorydialog.ui \
  $$PWD/importorderdialog.ui \
  $$PWD/incdecpricesdialog.ui \
  $$PWD/selectcolordialog.ui \
  $$PWD/selectdocumentdialog.ui \
  $$PWD/selectitemdialog.ui \
  $$PWD/selectreportdialog.ui \
  $$PWD/settingsdialog.ui \
  $$PWD/settopriceguidedialog.ui

HEADERS += $$replace(FORMS, '\\.ui', '.h')
SOURCES += $$replace(FORMS, '\\.ui', '.cpp')

}

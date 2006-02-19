## Copyright (C) 2004-2005 Robert Griebl.  All rights reserved.
##
## This file is part of BrickStore.
##
## This file may be distributed and/or modified under the terms of the GNU 
## General Public License version 2 as published by the Free Software Foundation 
## and appearing in the file LICENSE.GPL included in the packaging of this file.
##
## This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
## WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
##
## See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.

TEMPLATE     = app
CONFIG      *= warn_on thread qt link_prl

TARGET       = brickstore

TRANSLATIONS = translations/brickstore_de.ts \
               translations/brickstore_fr.ts

res_images          = images/*.png images/*.jpg 
res_images_16       = images/16x16/*.png
res_images_22       = images/22x22/*.png
res_images_status   = images/status/*.png
res_translations    = $$TRANSLATIONS
res_print_templates = print-templates/*.xml

dist_extra          = version.h.in _RELEASE_ icon.png
dist_scripts        = scripts/*.sh scripts/*.js scripts/*.pl
dist_unix_rpm       = rpm/create.sh rpm/brickstore.spec
dist_unix_deb       = debian/create.sh debian/rules
dist_macx           = macx-bundle/create.sh macx-bundle/install-table.txt macx-bundle/*.plist macx-bundle/Resources/*.icns macx-bundle/Resources/??.lproj/*.plist
dist_win32          = win32-installer/*.wx?

DISTFILES += $$res_images $$res_images_16 $$res_images_22 $$res_images_status $$res_translations $$res_print_templates $$dist_extra $$dist_scripts $$dist_unix_rpm $$dist_unix_deb $$dist_macx $$dist_win32

MOC_DIR   = .moc
UI_DIR    = .uic

win32 {
  system( scripts\update_version.js )

  LIBS += libcurl.lib
  DEFINES += CURL_STATICLIB
  RC_FILE = brickstore.rc
  QMAKE_CXXFLAGS_DEBUG += /Od
}

unix {
  OBJECTS_DIR = .obj  # grrr ... f***ing msvc.net doesn't link with this line present

  system( scripts/update_version.sh )
}

unix:!macx {
  LIBS += -lcurl

  isEmpty( PREFIX ):PREFIX = /usr/local
  
  target.path = $$PREFIX/bin
  resources1.path = $$PREFIX/share/brickstore/images
  resources1.files = $$res_images
  resources2.path = $$PREFIX/share/brickstore/images/16x16
  resources2.files = $$res_images_16
  resources3.path = $$PREFIX/share/brickstore/images/22x22
  resources3.files = $$res_images_22
  resources4.path = $$PREFIX/share/brickstore/images/status
  resources4.files = $$res_images_status
  resources5.path = $$PREFIX/share/brickstore/translations
  resources5.files = $$res_translations
  resources6.path = $$PREFIX/share/brickstore/print-templates
  resources6.files = $$res_print_templates

  # this does not work, since qmake loads the qt prl after processing this file...
  #!contains( CONFIG, shared ):resources5.extra = cp $(QTDIR)/translations/qt_de.qm translations

  INSTALLS += target resources1 resources2 resources3 resources4 resources5 resources6
}

macx {
  # HACK, but we need the abs. path, since MacOS X already has an old 2.0.2 version in /usr/lib
  LIBS += /usr/local/lib/libcurl.a
}


HEADERS += bricklink.h \
           capplication.h \
           cconfig.h \
           cframework.h \
           ciconfactory.h \
           cinfobar.h \
           citemtypecombo.h \
           citemview.h \
           clistaction.h \
           clistview.h \
           cmessagebox.h \
           cmoney.h \
           cmultiprogressbar.h \
           cpicturewidget.h \
           cpriceguidewidget.h \
           cref.h \
           creport.h \
           creport_p.h \
           cresource.h \
           cselectcolor.h \
           cselectitem.h \
           ctaskbar.h \
           ctransfer.h \
           curllabel.h \
           cutility.h \
           cwindow.h

SOURCES += bricklink.cpp \
           bricklink_data.cpp \
           bricklink_picture.cpp \
           bricklink_inventory.cpp \
           bricklink_priceguide.cpp \
           capplication.cpp \
           cconfig.cpp \
           cframework.cpp \
           ciconfactory.cpp \
           cinfobar.cpp \
           citemview.cpp \
           clistaction.cpp \
           clistview.cpp \
           cmessagebox.cpp \
           cmoney.cpp \
           cmultiprogressbar.cpp \
           cpicturewidget.cpp \
           cpriceguidewidget.cpp \
           cref.cpp \
           creport.cpp \
           cresource.cpp \
           cselectcolor.cpp \
           cselectitem.cpp \
           ctaskbar.cpp \
           ctransfer.cpp \
           curllabel.cpp \
           cutility.cpp \
           cwindow.cpp \
           main.cpp

FORMS   += dlgadditem.ui \
           dlgdbupdate.ui \
           dlgincdecprice.ui \
           dlgincompleteitem.ui \
           dlgloadinventory.ui \
           dlgloadorder.ui \
           dlgmerge.ui \
           dlgmessage.ui \
           dlgreportui.ui \
           dlgselectreport.ui \
           dlgsetcondition.ui \
           dlgsettings.ui \
           dlgsettopg.ui \
           dlgsubtractitem.ui \
           dlgupdate.ui

HEADERS += dlgadditemimpl.h \
           dlgdbupdateimpl.h \
           dlgincdecpriceimpl.h \
           dlgincompleteitemimpl.h \
           dlgloadinventoryimpl.h \
           dlgloadorderimpl.h \
           dlgmergeimpl.h \
           dlgmessageimpl.h \
           dlgreportuiimpl.h \
           dlgselectreportimpl.h \
           dlgsetconditionimpl.h \
           dlgsettingsimpl.h \
           dlgsettopgimpl.h \
           dlgsubtractitemimpl.h \
           dlgupdateimpl.h

SOURCES += dlgadditemimpl.cpp \
           dlgdbupdateimpl.cpp \
           dlgincdecpriceimpl.cpp \
           dlgincompleteitemimpl.cpp \
           dlgloadinventoryimpl.cpp \
           dlgloadorderimpl.cpp \
           dlgmergeimpl.cpp \
           dlgmessageimpl.cpp \
           dlgreportuiimpl.cpp \
           dlgselectreportimpl.cpp \
           dlgsetconditionimpl.cpp \
           dlgsettingsimpl.cpp \
           dlgsettopgimpl.cpp \
           dlgsubtractitemimpl.cpp \
           dlgupdateimpl.cpp

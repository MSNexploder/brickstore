/* Copyright (C) 2004-2005 Robert Griebl.  All rights reserved.
**
** This file is part of BrickStore.
**
** This file may be distributed and/or modified under the terms of the GNU 
** General Public License version 2 as published by the Free Software Foundation 
** and appearing in the file LICENSE.GPL included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.
*/
#include <qtimer.h>
#include <qevent.h>
#include <qdir.h>

#if defined( Q_OS_UNIX )
#include <sys/utsname.h>
#endif

#include "cconfig.h"
#include "cmoney.h"
#include "cresource.h"
#include "cframework.h"
#include "cmessagebox.h"
#include "creport.h"
#include "bricklink.h"
#include "version.h"
#include "dlgmessageimpl.h"
#include "dlgregistrationimpl.h"
#include "crebuilddatabase.h"
#include "cprogressdialog.h"
#include "ccheckforupdates.h"
#include "csplash.h"

#include "capplication.h"


class COpenEvent : public QCustomEvent {
public:
	enum { Type = QEvent::User + 142 };

	COpenEvent ( const QString &file ) 
		: QCustomEvent ( Type ), m_file ( file ) 
	{ }
	
	QString fileName ( ) const 
	{ return m_file; }
	
private:
	QString m_file;
};


CApplication *cApp = 0;                

CApplication::CApplication ( const char *rebuild_db_only, int _argc, char **_argv ) 
	: QApplication ( _argc, _argv, !rebuild_db_only )
{
	cApp = this;

	m_enable_emit = false;
	m_rebuild_db_only = rebuild_db_only;

#if defined( Q_WS_MACX )
	AEInstallEventHandler( kCoreEventClass, kAEOpenDocuments, appleEventHandler, 0, false );
#endif

	// initialize config & resource
	(void) CConfig::inst ( )-> upgrade ( BRICKSTORE_MAJOR, BRICKSTORE_MINOR, BRICKSTORE_PATCH );
	(void) CMoney::inst ( );
	(void) CResource::inst ( );
	(void) CReportManager::inst ( );

	m_trans_qt = 0;
	m_trans_brickstore = 0;

	initStrings ( );

	if ( !initBrickLink ( )) {
		// we cannot call quit directly, since there is
		// no event loop to quit from...
		postEvent ( this, new QEvent ( QEvent::Quit ));
		return;
	}
	else if ( !m_rebuild_db_only. isEmpty ( )) {	
		QTimer::singleShot ( 0, this, SLOT( rebuildDatabase ( )));
	}
	else {
		updateTranslations ( );
		connect ( CConfig::inst ( ), SIGNAL( languageChanged ( )), this, SLOT( updateTranslations ( )));
		
		CSplash::inst ( );

		CMessageBox::setDefaultTitle ( appName ( ));

		QPixmap pix;
	
		pix = CResource::inst ( )-> pixmap ( "icon" );
		QMimeSourceFactory::defaultFactory ( )-> setImage ( "brickstore-icon", pix. convertToImage ( ));
		pix = CResource::inst ( )-> pixmap ( "important" );
		QMimeSourceFactory::defaultFactory ( )-> setImage ( "brickstore-important", pix. convertToImage ( ));

		for ( int i = 1; i < argc ( ); i++ )
			m_files_to_open << argv ( ) [i];
	
		CSplash::inst ( )-> finish ( CFrameWork::inst ( ));
		CFrameWork::inst ( )-> show ( );

		while ( CConfig::inst ( )-> registration ( ) == CConfig::None ) {
			DlgRegistrationImpl d ( true, mainWidget ( ));
			d. exec ( );			
		}
		demoVersion ( );
		connect ( CConfig::inst ( ), SIGNAL( registrationChanged ( CConfig::Registration )), this, SLOT( demoVersion ( )));
	}
}
	
CApplication::~CApplication ( )
{
#if defined( Q_WS_MACX )
	AERemoveEventHandler ( kCoreEventClass, kAEOpenDocuments, appleEventHandler, false );
#endif
	exitBrickLink ( );

	QMimeSourceFactory::defaultFactory ( )-> setData ( "brickstore-icon", 0 );

	delete CReportManager::inst ( );
	delete CResource::inst ( );
	delete CMoney::inst ( );
	delete CConfig::inst ( );
}

void CApplication::updateTranslations ( )
{
	QString locale = CConfig::inst ( )-> language ( );
	if ( locale. isEmpty ( ))
		locale = QLocale::system ( ). name ( );
	QLocale::setDefault ( QLocale ( locale ));

	if ( m_trans_qt )
		removeTranslator ( m_trans_qt );
	if ( m_trans_brickstore )
		removeTranslator ( m_trans_brickstore );

	m_trans_qt = CResource::inst ( )-> translation ( "qt", locale );
	if ( m_trans_qt )
		installTranslator ( m_trans_qt );

	m_trans_brickstore = CResource::inst ( )-> translation ( "brickstore", locale );
	if ( m_trans_brickstore )
		installTranslator ( m_trans_brickstore );
}

void CApplication::rebuildDatabase ( )
{
	CRebuildDatabase rdb ( m_rebuild_db_only );
	exit ( rdb. exec ( ));
}


QString CApplication::appName ( ) const
{
	return "BrickStore";
}

QString CApplication::appVersion ( ) const
{
	return BRICKSTORE_VERSION;
}

QString CApplication::appURL ( ) const
{
	return BRICKSTORE_URL;
}

QString CApplication::sysName ( ) const
{
	return m_sys_name;
}

QString CApplication::sysVersion ( ) const
{
	return m_sys_version;
}

void CApplication::enableEmitOpenDocument ( bool b )
{
	if ( b != m_enable_emit ) {
		m_enable_emit = b;
		
		if ( b && !m_files_to_open. isEmpty ( ))
			QTimer::singleShot ( 0, this, SLOT( doEmitOpenDocument ( )));
	}
}

void CApplication::doEmitOpenDocument ( )
{
	while ( m_enable_emit && !m_files_to_open. isEmpty ( )) {
		QString file = m_files_to_open. front ( );
		m_files_to_open. pop_front ( );
		
		emit openDocument ( file );
	}
}

void CApplication::customEvent ( QCustomEvent *e )
{
	if ( int( e-> type ( )) == int( COpenEvent::Type )) {
		m_files_to_open. append ( static_cast <COpenEvent *> ( e )-> fileName ( ));
		
		doEmitOpenDocument ( );
	}
}
   	
#if defined( Q_WS_MACX )
OSErr CApplication::appleEventHandler ( const AppleEvent *event, AppleEvent *, long )
{
	AEDescList docs;
	
	if ( AEGetParamDesc ( event, keyDirectObject, typeAEList, &docs ) == noErr ) {
		long n = 0;
		AECountItems ( &docs, &n );
		UInt8 strbuffer [PATH_MAX];
		for ( long i = 0; i < n; i++ ) {
			FSRef ref;
			if ( AEGetNthPtr ( &docs, i + 1, typeFSRef, 0, 0, &ref, sizeof( ref ), 0) != noErr )
				continue;
			if ( FSRefMakePath ( &ref, strbuffer, 256 ) == noErr ) {
				COpenEvent e ( QString::fromUtf8 ( reinterpret_cast <char *> ( strbuffer )));
				QApplication::sendEvent ( qApp, &e );
			}
		}	
	}
	return noErr;
}
#endif


bool CApplication::initBrickLink ( )
{
	QString errstring;
	QString defdatadir = QDir::homeDirPath ( );

#if defined( Q_OS_WIN32 )
	defdatadir += "/brickstore-cache/";
#else
	defdatadir += "/.brickstore-cache/";
#endif

	BrickLink *bl = BrickLink::inst ( CConfig::inst ( )-> readEntry ( "/BrickLink/DataDir", defdatadir ), &errstring );

	if ( !bl )
		CMessageBox::critical ( 0, tr( "Could not initialize the BrickLink kernel:<br /><br />%1" ). arg ( errstring ));

	return ( bl != 0 );
}


void CApplication::exitBrickLink ( )
{
	delete BrickLink::inst ( );
}


void CApplication::initStrings ( )
{
	m_legal = QT_TR_NOOP(
		"<p>"
		"This program is free software; it may be distributed and/or modified "
		"under the terms of the GNU General Public License version 2 as published "
		"by the Free Software Foundation and appearing in the file LICENSE.GPL "
		"included in this software package."
		"<br />"
		"This program is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE "
		"WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE."
		"<br />"
		"See <a href=\"http://fsf.org/licensing/licenses/gpl.html\">www.fsf.org/licensing/licenses/gpl.html</a> for GPL licensing information."
		"</p><p>"
		"All data from <a href=\"http://www.bricklink.com\">www.bricklink.com</a> is owned by BrickLink<sup>TM</sup>, "
		"which is a trademark of Dan Jezek."
		"</p><p>"
		"Peeron Inventories from <a href=\"http://www.peeron.com\">www.peeron.com</a> are owned by Dan and Jennifer Boger."
		"</p><p>"
		"LEGO<sup>&reg;</sup> is a trademark of the LEGO group of companies, "
		"which does not sponsor, authorize or endorse this software."
		"</p><p>"
		"All other trademarks recognised."
		"</p><p>"
		"French translation by Sylvain Perez (<a href=\"mailto:bricklink@1001bricks.com\">1001bricks</a>)"
		"</p>"
	);

	m_demo = QT_TR_NOOP(
		"This is an <b>unrestricted</b> demo version."
		"<br /><br />"
		"If you want to support the development of this program "
		"(or if you just want to get rid of this dialog...), "
		"check out %1 how to get the non-demo version." 
	);

	m_url  = "<a href=\"http://" BRICKSTORE_URL "\">" BRICKSTORE_URL "</a>";
	m_mail = "<a href=\"mailto:" BRICKSTORE_MAIL "\">" BRICKSTORE_MAIL "</a>";

	m_sys_name    = "(unknown)";
	m_sys_version = "(unknown)";

#if defined( Q_OS_MACX )
	m_sys_name = "MacOS";

	switch ( QApplication::macVersion ( )) {
		case Qt::MV_10_DOT_0: m_sys_version = "10.0 (Cheetah)"; break;
		case Qt::MV_10_DOT_1: m_sys_version = "10.1 (Puma)";    break;
		case Qt::MV_10_DOT_2: m_sys_version = "10.2 (Jaguar)";  break;
		case Qt::MV_10_DOT_3: m_sys_version = "10.3 (Panther)"; break;
		case Qt::MV_10_DOT_4: m_sys_version = "10.4 (Tiger)";   break;
		default             : break;
	}

#elif defined( Q_OS_WIN )
	m_sys_name = "Windows";

	switch ( QApplication::winVersion ( )) {
		case Qt::WV_95  : m_sys_version = "95";   break;
		case Qt::WV_98  : m_sys_version = "98";   break;
		case Qt::WV_Me  : m_sys_version = "ME";   break;
		case Qt::WV_NT  : m_sys_version = "NT";   break;
		case Qt::WV_2000: m_sys_version = "2000"; break;
		case Qt::WV_XP  : m_sys_version = "XP";   break;
		case Qt::WV_2003: m_sys_version = "2003"; break;
		default         : break;
	}

#elif defined( Q_OS_UNIX )
	m_sys_name = "Unix";

	struct ::utsname utsinfo;
	if ( ::uname ( &utsinfo ) >= 0 )
		m_sys_version = QString( utsinfo. sysname ) + " " + utsinfo. machine;

#endif
}


void CApplication::about ( )
{
	static const char *layout_text =
		"<qt><center>"
			"<table border=\"0\"><tr>"
				"<td valign=\"middle\" align=\"right\"><img src=\"brickstore-icon\" /></td>"
				"<td align=\"left\"><big>"
					"<big><strong>%1</strong></big>"
					"<br />%2<br />"
					"<strong>%3</strong>"
				"</big></td>"
			"</tr></table>"
			"<br />%4"
		"</center>%5</qt>";

	QString copyright = tr( "Copyright &copy; %1" ). arg ( BRICKSTORE_COPYRIGHT );
	QString version   = tr( "Version %1" ). arg ( BRICKSTORE_VERSION );
	QString support   = tr( "Visit %1, or send an email to %2" ). arg ( m_url ). arg ( m_mail );

	QString text = QString ( layout_text ). arg ( appName ( )). arg ( copyright, version, support, tr( m_legal ));

	DlgMessageImpl d ( appName ( ), text, false, mainWidget ( ));
	d. exec ( );
}

void CApplication::demoVersion ( )
{
	if ( CConfig::inst ( )-> registration ( ) != CConfig::Demo )
		return;

	static const char *layout_text =
		"<qt><center>"
			"<table border=\"0\"><tr>"
				"<td valign=\"middle\" align=\"right\"><img src=\"brickstore-icon\" /></td>"
				"<td align=\"left\"><big>"
					"<big><strong>%1</strong></big>"
					"<br />%2<br />"
					"<strong>%3</strong>"
				"</big></td>"
			"</tr></table>"
			"<br />%4"
		"</center</qt>";

	QString copyright = tr( "Copyright &copy; %1" ). arg ( BRICKSTORE_COPYRIGHT );
	QString version   = tr( "Version %1" ). arg ( BRICKSTORE_VERSION );

	QString text = QString ( layout_text ). arg ( appName ( ), copyright, version, tr( m_demo ). arg ( m_url ));

	DlgMessageImpl d ( appName ( ), text, true, mainWidget ( ));
	d. exec ( );

	QTimer::singleShot ( 20*60*1000, this, SLOT( demoVersion ( )));
}

void CApplication::checkForUpdates ( )
{
	CProgressDialog d ( mainWidget ( ));
	CCheckForUpdates cfu ( &d );
	d. exec ( );
}

void CApplication::registration ( )
{
	DlgRegistrationImpl d ( false, mainWidget ( ));
	d. exec ( );
}

/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2013, Uwe L. Korn <uwelk@xhochy.com>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "NetworkActivityWidget_p.h"

#include "audio/AudioEngine.h"
#include "database/Database.h"
#include "database/DatabaseCommand_NetworkCharts.h"
#include "database/DatabaseCommand_TrendingTracks.h"
#include "playlist/AlbumItemDelegate.h"
#include "playlist/RecentlyLovedTracksModel.h"
#include "playlist/TopLovedTracksModel.h"
#include "playlist/TreeProxyModel.h"
#include "playlist/ViewHeader.h"
#include "utils/AnimatedSpinner.h"
#include "utils/ImageRegistry.h"
#include "utils/Logger.h"
#include "utils/TomahawkStyle.h"
#include "utils/TomahawkUtilsGui.h"
#include "utils/DpiScaler.h"
#include "widgets/OverlayWidget.h"
#include "widgets/PlaylistsModel.h"
#include "widgets/RecentlyPlayedPlaylistsModel.h"
#include "MetaPlaylistInterface.h"
#include "Pipeline.h"
#include "PlaylistDelegate.h"
#include "ViewManager.h"

#include <QDateTime>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QtConcurrentRun>

using namespace Tomahawk;
using namespace Tomahawk::Widgets;

NetworkActivityWidget::NetworkActivityWidget( QWidget* parent )
    : QWidget( parent )
    , d_ptr( new NetworkActivityWidgetPrivate ( this ) )
{
    Q_D( NetworkActivityWidget );
    QWidget* widget = new QWidget();

    d->ui->setupUi( widget );

    d->crumbModelLeft = new QStandardItemModel( this );
    d->sortedProxy = new QSortFilterProxyModel( this );

    // d_func()->ui->breadCrumbLeft->setRootIcon( TomahawkUtils::defaultPixmap( TomahawkUtils::NetworkActivity, TomahawkUtils::Original ) );
    connect( d_func()->ui->breadCrumbLeft, SIGNAL( activateIndex( QModelIndex ) ), SLOT( leftCrumbIndexChanged( QModelIndex ) ) );

    // Build up breadcrumb
    QStandardItem* rootItem = d_func()->crumbModelLeft->invisibleRootItem();
    // Breadcumps for Charts
    {
        QStandardItem* chartItem = new QStandardItem( tr( "Charts" ) );
        rootItem->appendRow( chartItem );
        QStandardItem* overallItem = new QStandardItem( tr( "Overall" ) );
        overallItem->setData( OverallChart, Breadcrumb::DefaultRole );
        chartItem->appendRow( overallItem );
        QStandardItem* yearItem = new QStandardItem( tr( "Last Year" ) );
        yearItem->setData( YearChart, Breadcrumb::DefaultRole );
        chartItem->appendRow( yearItem );
        QStandardItem* monthItem = new QStandardItem( tr( "Last Month" ) );
        monthItem->setData( MonthChart, Breadcrumb::DefaultRole );
        chartItem->appendRow( monthItem );
        QStandardItem* weekItem = new QStandardItem( tr( "Last Week" ) );
        weekItem->setData( WeekChart, Breadcrumb::DefaultRole );
        chartItem->appendRow( weekItem );
    }
    // Breadcrumbs for Loved Tracks
    {
        QStandardItem* lovedItem = new QStandardItem( tr( "Loved Tracks" ) );
        rootItem->appendRow( lovedItem );
        QStandardItem* topItem = new QStandardItem( tr( "Top Loved" ) );
        topItem->setData( TopLoved, Breadcrumb::DefaultRole );
        lovedItem->appendRow( topItem );
        QStandardItem* recentlyItem = new QStandardItem( tr( "Recently Loved" ) );
        recentlyItem->setData( RecentlyLoved, Breadcrumb::DefaultRole );
        lovedItem->appendRow( recentlyItem );
    }
    d->sortedProxy->setSourceModel( d->crumbModelLeft );
    d->ui->breadCrumbLeft->setModel( d->sortedProxy );
    d->ui->breadCrumbLeft->setVisible( true );


    {
        AlbumItemDelegate* del = new AlbumItemDelegate( d->ui->tracksViewLeft, d->ui->tracksViewLeft->proxyModel(), true );
        d->ui->tracksViewLeft->setPlaylistItemDelegate( del );
        d->ui->tracksViewLeft->proxyModel()->setStyle( PlayableProxyModel::Short );
        d->ui->tracksViewLeft->setAutoResize( true );
        d->ui->tracksViewLeft->setAlternatingRowColors( false );
        d->ui->tracksViewLeft->setSortingEnabled( false );
        d->ui->tracksViewLeft->setEmptyTip( tr( "Sorry, we are still loading the charts." ) );

        QPalette p = d->ui->tracksViewLeft->palette();
        p.setColor( QPalette::Text, TomahawkStyle::PAGE_TRACKLIST_TRACK_SOLVED );
        p.setColor( QPalette::BrightText, TomahawkStyle::PAGE_TRACKLIST_TRACK_UNRESOLVED );
        p.setColor( QPalette::Foreground, TomahawkStyle::PAGE_TRACKLIST_NUMBER );
        p.setColor( QPalette::Highlight, TomahawkStyle::PAGE_TRACKLIST_HIGHLIGHT );
        p.setColor( QPalette::HighlightedText, TomahawkStyle::PAGE_TRACKLIST_HIGHLIGHT_TEXT );

        d->ui->tracksViewLeft->setPalette( p );
        d->ui->tracksViewLeft->setFrameShape( QFrame::NoFrame );
        d->ui->tracksViewLeft->setAttribute( Qt::WA_MacShowFocusRect, 0 );
        d->ui->tracksViewLeft->setStyleSheet( "QTreeView { background-color: transparent; }" );

        TomahawkStyle::stylePageFrame( d->ui->chartsFrame );
    }


    // Trending Tracks
    {
        d->trendingTracksModel = new PlaylistModel( d->ui->trendingTracksView );
        d->ui->trendingTracksView->proxyModel()->setStyle( PlayableProxyModel::Short );
        d->ui->trendingTracksView->overlay()->setEnabled( false );
        d->ui->trendingTracksView->setPlaylistModel( d->trendingTracksModel );
        d->ui->trendingTracksView->setAutoResize( true );
        d->ui->trendingTracksView->setAlternatingRowColors( false );

        QPalette p = d->ui->trendingTracksView->palette();
        p.setColor( QPalette::Text, TomahawkStyle::PAGE_TRACKLIST_TRACK_SOLVED );
        p.setColor( QPalette::BrightText, TomahawkStyle::PAGE_TRACKLIST_TRACK_UNRESOLVED );
        p.setColor( QPalette::Foreground, TomahawkStyle::PAGE_TRACKLIST_NUMBER );
        p.setColor( QPalette::Highlight, TomahawkStyle::PAGE_TRACKLIST_HIGHLIGHT );
        p.setColor( QPalette::HighlightedText, TomahawkStyle::PAGE_TRACKLIST_HIGHLIGHT_TEXT );

        d->ui->trendingTracksView->setPalette( p );
        d->ui->trendingTracksView->setFrameShape( QFrame::NoFrame );
        d->ui->trendingTracksView->setAttribute( Qt::WA_MacShowFocusRect, 0 );
        d->ui->trendingTracksView->setStyleSheet( "QTreeView { background-color: transparent; }" );

        TomahawkStyle::stylePageFrame( d->ui->trendingTracksFrame );
    }
    {
        QFont f = d->ui->trendingTracksLabel->font();
        f.setFamily( "Pathway Gothic One" );

        QPalette p = d->ui->trendingTracksLabel->palette();
        p.setColor( QPalette::Foreground, TomahawkStyle::PAGE_CAPTION );

        d->ui->trendingTracksLabel->setFont( f );
        d->ui->trendingTracksLabel->setPalette( p );
    }

    // Hot Playlists
    {
        TomahawkStyle::stylePageFrame( d->ui->playlistsFrame );

        QFont f = d->ui->hotPlaylistsLabel->font();
        f.setFamily( "Pathway Gothic One" );

        QPalette p = d->ui->hotPlaylistsLabel->palette();
        p.setColor( QPalette::Foreground, TomahawkStyle::PAGE_CAPTION );

        d->ui->hotPlaylistsLabel->setFont( f );
        d->ui->hotPlaylistsLabel->setPalette( p );
    }

    {
        d->ui->playlistView->setFrameShape( QFrame::NoFrame );
        d->ui->playlistView->setAttribute( Qt::WA_MacShowFocusRect, 0 );
        d->ui->playlistView->setItemDelegate( new PlaylistDelegate() );
        d->ui->playlistView->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );

        QPalette p = d->ui->playlistView->palette();
        p.setColor( QPalette::Text, TomahawkStyle::HEADER_TEXT );
        p.setColor( QPalette::BrightText, TomahawkStyle::HEADER_TEXT );
        p.setColor( QPalette::Foreground, TomahawkStyle::HEADER_TEXT );
        p.setColor( QPalette::Highlight, TomahawkStyle::HEADER_TEXT );
        p.setColor( QPalette::HighlightedText, TomahawkStyle::HEADER_BACKGROUND );

        d->ui->playlistView->setPalette( p );
        d->ui->playlistView->overlay()->setPalette( p );
        d->ui->playlistView->setStyleSheet( "QListView { background-color: transparent; }" );
        TomahawkStyle::styleScrollBar( d->ui->playlistView->verticalScrollBar() );
        TomahawkStyle::stylePageFrame( d->ui->playlistsFrame );

        connect( d->ui->playlistView, SIGNAL( activated( QModelIndex ) ), SLOT( onPlaylistActivated( QModelIndex ) ) );
    }

    // Trending artists
    {
        d->artistsModel = new PlayableModel( d->ui->trendingArtistsView );
        d->ui->trendingArtistsView->setPlayableModel( d->artistsModel );
    }
    {
        d->ui->trendingArtistsView->setFrameShape( QFrame::NoFrame );
        d->ui->trendingArtistsView->setAttribute( Qt::WA_MacShowFocusRect, 0 );
        d->ui->trendingArtistsView->proxyModel()->sort( -1 );
        d->ui->trendingArtistsView->proxyModel()->setHideDupeItems( true );

        d->ui->trendingArtistsView->setAutoResize( true );
        d->ui->trendingArtistsView->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        d->ui->trendingArtistsView->setStyleSheet( "QListView { background-color: transparent; }" );
        TomahawkStyle::stylePageFrame( d->ui->trendingArtistsFrame );
    }
    {
        QFont f = d->ui->trendingArtistsLabel->font();
        f.setFamily( "Pathway Gothic One" );

        QPalette p = d->ui->trendingArtistsLabel->palette();
        p.setColor( QPalette::Foreground, TomahawkStyle::PAGE_CAPTION );

        d->ui->trendingArtistsLabel->setFont( f );
        d->ui->trendingArtistsLabel->setPalette( p );
    }


    {
        QScrollArea* area = new QScrollArea();
        area->setWidgetResizable( true );
        area->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOn );
        area->setWidget( widget );

        QVBoxLayout* layout = new QVBoxLayout();
        layout->addWidget( area );
        setLayout( layout );
        TomahawkUtils::unmarginLayout( layout );
    }

    MetaPlaylistInterface* mpl = new MetaPlaylistInterface();
    mpl->addChildInterface( d->ui->trendingTracksView->playlistInterface() );
    mpl->addChildInterface( d->ui->tracksViewLeft->playlistInterface() );
    d->playlistInterface = playlistinterface_ptr( mpl );


    // Load data in separate thread
    d->workerThread = new QThread();
    d->workerThread->start();
    d->worker = new NetworkActivityWorker( );
    d->worker->moveToThread( d->workerThread );
    connect( d->worker, SIGNAL( trendingTracks( QList<Tomahawk::track_ptr> ) ),
             SLOT( trendingTracks( QList<Tomahawk::track_ptr> ) ),
             Qt::QueuedConnection);
    connect( d->worker, SIGNAL( hotPlaylists(QList<Tomahawk::playlist_ptr>) ),
             SLOT(hotPlaylists(QList<Tomahawk::playlist_ptr>)),
             Qt::QueuedConnection);
    connect( d->worker, SIGNAL( trendingArtists( QList< Tomahawk::artist_ptr > ) ),
             SLOT( trendingArtists( QList< Tomahawk::artist_ptr > ) ),
             Qt::QueuedConnection );
    connect( d->worker, SIGNAL( finished() ),
             d->workerThread, SLOT( quit() ),
             Qt::QueuedConnection );
    // connect( d->workerThread, SIGNAL( finished() ), d->workerThread, SLOT( deleteLater() ), Qt::QueuedConnection );
    // connect( d->workerThread, SIGNAL( destroyed() ), d->worker, SLOT( deleteLater() ), Qt::QueuedConnection );
    QMetaObject::invokeMethod( d->worker, "run", Qt::QueuedConnection );
}


NetworkActivityWidget::~NetworkActivityWidget()
{
}


Tomahawk::playlistinterface_ptr
NetworkActivityWidget::playlistInterface() const
{
    return d_func()->playlistInterface;
}


QPixmap
NetworkActivityWidget::pixmap() const
{
    return TomahawkUtils::defaultPixmap( TomahawkUtils::NetworkActivity,
                                         TomahawkUtils::Original,
                                         TomahawkUtils::DpiScaler::scaled( this, 80, 80 ) );
}


bool
NetworkActivityWidget::isBeingPlayed() const
{
    Q_D( const NetworkActivityWidget );

    if ( AudioEngine::instance()->currentTrackPlaylist() == d->ui->tracksViewLeft->playlistInterface() )
        return true;

    if ( AudioEngine::instance()->currentTrackPlaylist() == d->ui->trendingTracksView->playlistInterface() )
        return true;

    return false;
}


bool
NetworkActivityWidget::jumpToCurrentTrack()
{
    Q_D( NetworkActivityWidget );

    if ( d->ui->tracksViewLeft->model() && d_func()->ui->tracksViewLeft->jumpToCurrentTrack() )
        return true;

    if ( d->ui->trendingTracksView->model() && d_func()->ui->trendingTracksView->jumpToCurrentTrack() )
        return true;

    return false;
}


void
NetworkActivityWidget::weeklyCharts( const QList<Tomahawk::track_ptr>& tracks )
{
    d_func()->weeklyChartsModel = new PlaylistModel( d_func()->ui->tracksViewLeft );
    d_func()->weeklyChartsModel->startLoading();
    d_func()->weeklyChartsModel->appendTracks( tracks );
    d_func()->weeklyChartsModel->finishLoading();

    if ( d_func()->activeView == WeekChart )
    {
        showWeekCharts();
    }
}


void
NetworkActivityWidget::monthlyCharts( const QList<Tomahawk::track_ptr>& tracks )
{
    d_func()->monthlyChartsModel = new PlaylistModel( d_func()->ui->tracksViewLeft );
    d_func()->monthlyChartsModel->startLoading();
    d_func()->monthlyChartsModel->appendTracks( tracks );
    d_func()->monthlyChartsModel->finishLoading();

    if ( d_func()->activeView == MonthChart )
    {
        showMonthCharts();
    }
}


void
NetworkActivityWidget::yearlyCharts( const QList<Tomahawk::track_ptr>& tracks )
{
    d_func()->yearlyChartsModel = new PlaylistModel( d_func()->ui->tracksViewLeft );
    d_func()->yearlyChartsModel->startLoading();
    d_func()->yearlyChartsModel->appendTracks( tracks );
    d_func()->yearlyChartsModel->finishLoading();

    if ( d_func()->activeView == YearChart )
    {
        showYearCharts();
    }
}


void
NetworkActivityWidget::overallCharts( const QList<track_ptr>& tracks )
{
    d_func()->overallChartsModel = new PlaylistModel( d_func()->ui->tracksViewLeft );
    d_func()->overallChartsModel->startLoading();
    d_func()->overallChartsModel->appendTracks( tracks );
    d_func()->overallChartsModel->finishLoading();

    if ( d_func()->activeView == OverallChart )
    {
        showOverallCharts();
    }
}

void
NetworkActivityWidget::hotPlaylists( const QList<playlist_ptr>& playlists )
{
    Q_D( NetworkActivityWidget );
    d->ui->playlistView->setModel( new PlaylistsModel( playlists, this ) );
}


void
NetworkActivityWidget::trendingArtists( const QList<artist_ptr>& artists )
{
    Q_D( NetworkActivityWidget );

    d->artistsModel->startLoading();
    d->artistsModel->appendArtists( artists );
    d->artistsModel->finishLoading();
}


void
NetworkActivityWidget::trendingTracks( const QList<track_ptr>& tracks )
{
    Q_D( NetworkActivityWidget );

    d->trendingTracksModel->startLoading();
    d->trendingTracksModel->appendTracks( tracks );
    d->trendingTracksModel->finishLoading();
}


void
NetworkActivityWidget::leftCrumbIndexChanged( const QModelIndex& index )
{
    QStandardItem* item = d_func()->crumbModelLeft->itemFromIndex( d_func()->sortedProxy->mapToSource( index ) );
    if ( !item )
        return;
    if ( !item->data( Breadcrumb::DefaultRole ).isValid() )
        return;

    int chartId = item->data( Breadcrumb::DefaultRole ).toInt();
    tLog( LOGVERBOSE ) << Q_FUNC_INFO << "Showing chart" << chartId;
    switch ( chartId )
    {
    case WeekChart:
        showWeekCharts();
        break;
    case MonthChart:
        showMonthCharts();
        break;
    case YearChart:
        showYearCharts();
        break;
    case OverallChart:
        showOverallCharts();
        break;
    case TopLoved:
        showTopLoved();
        break;
    case RecentlyLoved:
        showRecentlyLoved();
        break;
    }
}


void
NetworkActivityWidget::onPlaylistActivated( const QModelIndex& item )
{
    Tomahawk::playlist_ptr pl = item.data( RecentlyPlayedPlaylistsModel::PlaylistRole ).value< Tomahawk::playlist_ptr >();
    ViewManager::instance()->show( pl );
}


void
NetworkActivityWidget::fetchYearCharts()
{
    QDateTime to = QDateTime::currentDateTime();
    QDateTime yearAgo = to.addYears( -1 );
    DatabaseCommand_NetworkCharts* yearCharts = new DatabaseCommand_NetworkCharts( yearAgo, to );
    yearCharts->setLimit( numberOfNetworkChartEntries );
    connect( yearCharts, SIGNAL( done( QList<Tomahawk::track_ptr> ) ), SLOT( yearlyCharts( QList<Tomahawk::track_ptr> ) ) );
    Database::instance()->enqueue( Tomahawk::dbcmd_ptr( yearCharts ) );
}


void
NetworkActivityWidget::fetchOverallCharts()
{
    DatabaseCommand_NetworkCharts* overallCharts = new DatabaseCommand_NetworkCharts();
    overallCharts->setLimit( numberOfNetworkChartEntries );
    connect( overallCharts, SIGNAL( done( QList<Tomahawk::track_ptr> ) ), SLOT( overallCharts( QList<Tomahawk::track_ptr> ) ) );
    Database::instance()->enqueue( Tomahawk::dbcmd_ptr( overallCharts ) );
}


void
NetworkActivityWidget::fetchWeekCharts()
{
    QDateTime to = QDateTime::currentDateTime();
    QDateTime weekAgo = to.addDays( -7 );
    DatabaseCommand_NetworkCharts* weekCharts = new DatabaseCommand_NetworkCharts( weekAgo, to );
    weekCharts->setLimit( numberOfNetworkChartEntries );
    connect( weekCharts, SIGNAL( done( QList<Tomahawk::track_ptr> ) ), SLOT( weeklyCharts( QList<Tomahawk::track_ptr> ) ) );
    Database::instance()->enqueue( Tomahawk::dbcmd_ptr( weekCharts ) );
}


void
NetworkActivityWidget::fetchMonthCharts()
{
    QDateTime to = QDateTime::currentDateTime();
    QDateTime monthAgo = to.addMonths( -1 );
    DatabaseCommand_NetworkCharts* monthCharts = new DatabaseCommand_NetworkCharts( monthAgo, to );
    monthCharts->setLimit( numberOfNetworkChartEntries );
    connect( monthCharts, SIGNAL( done( QList<Tomahawk::track_ptr> ) ), SLOT( monthlyCharts( QList<Tomahawk::track_ptr> ) ) );
    Database::instance()->enqueue( Tomahawk::dbcmd_ptr( monthCharts ) );
}


void
NetworkActivityWidget::showWeekCharts()
{
    Q_D( NetworkActivityWidget );
    d->activeView = WeekChart;
    if ( !d->weeklyChartsModel.isNull() )
    {
        d->ui->tracksViewLeft->setPlaylistModel( d_func()->weeklyChartsModel );
        d->ui->tracksViewLeft->setAutoResize( true );
    }
    else
    {
        fetchWeekCharts();
    }
}


void
NetworkActivityWidget::showMonthCharts()
{
    Q_D( NetworkActivityWidget );

    d->activeView = MonthChart;
    if ( !d_func()->monthlyChartsModel.isNull() )
    {
        d->ui->tracksViewLeft->setPlaylistModel( d_func()->monthlyChartsModel );
        d->ui->tracksViewLeft->setAutoResize( true );
    }
    else
    {
        fetchMonthCharts();
    }
}


void
NetworkActivityWidget::showYearCharts()
{
    Q_D( NetworkActivityWidget );

    d->activeView = YearChart;
    if ( !d->yearlyChartsModel.isNull() )
    {
        d->ui->tracksViewLeft->setPlaylistModel( d_func()->yearlyChartsModel );
        d->ui->tracksViewLeft->setAutoResize( true );
    }
    else
    {
        fetchYearCharts();
    }
}


void
NetworkActivityWidget::showOverallCharts()
{
    Q_D( NetworkActivityWidget );

    d->activeView = OverallChart;
    if ( !d->overallChartsModel.isNull() )
    {
        d->ui->tracksViewLeft->setPlaylistModel( d->overallChartsModel );
        d->ui->tracksViewLeft->setAutoResize( true );
    }
    else
    {
        fetchOverallCharts();
    }
}


void
NetworkActivityWidget::showTopLoved()
{
    Q_D( NetworkActivityWidget );

    d->activeView = TopLoved;
    if ( d->topLovedModel.isNull() )
    {
        TopLovedTracksModel* model = new TopLovedTracksModel( this );
        model->setLimit( numberOfNetworkChartEntries );
        model->setSource( source_ptr() );
        d->topLovedModel = model;
    }
    d->ui->tracksViewLeft->setPlaylistModel( d->topLovedModel );
    d->ui->tracksViewLeft->setAutoResize( true );
}


void
NetworkActivityWidget::showRecentlyLoved()
{
    Q_D( NetworkActivityWidget );

    d->activeView = RecentlyLoved;
    if ( d->recentlyLovedModel.isNull() )
    {
        RecentlyLovedTracksModel* model = new RecentlyLovedTracksModel( this );
        model->setLimit( numberOfNetworkChartEntries );
        model->setSource( source_ptr() );
        d->recentlyLovedModel = model;
    }

    d->ui->tracksViewLeft->setPlaylistModel( d->recentlyLovedModel );
    d->ui->tracksViewLeft->setAutoResize( true );
}

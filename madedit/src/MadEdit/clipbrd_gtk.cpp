
// madedit: fixed co-exist of normal Clipboard and PrimarySelection under GTK
// patched from wx-gtk-clipbrd.patch:
// Fixes primary selection problem in wxClipboard under Gtk
// http://sourceforge.net/tracker/index.php?func=detail&aid=1280049&group_id=9863&atid=309863

/////////////////////////////////////////////////////////////////////////////
// Name:        gtk/clipbrd.cpp
// Purpose:
// Author:      Robert Roebling
// Id:          $Id: clipbrd.cpp,v 1.64 2006/01/22 23:28:53 MR Exp $
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifdef __WXGTK__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "clipbrd_gtk.h"

#if wxUSE_CLIPBOARD

#include <wx/dataobj.h>
#include <wx/utils.h>
#include <wx/log.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

//-----------------------------------------------------------------------------
// thread system
//-----------------------------------------------------------------------------

#if wxUSE_THREADS
#endif

//-----------------------------------------------------------------------------
// data
//-----------------------------------------------------------------------------

GdkAtom  g_clipboardAtom   = 0;
GdkAtom  g_targetsAtom     = 0;

#if defined(__WXGTK20__) && wxUSE_UNICODE
extern GdkAtom g_altTextAtom;
#endif

// the trace mask we use with wxLogTrace() - call
// wxLog::AddTraceMask(TRACE_CLIPBOARD) to enable the trace messages from here
// (there will be a *lot* of them!)
static const wxChar *TRACE_CLIPBOARD = _T("clipboard");

//-----------------------------------------------------------------------------
// reminder
//-----------------------------------------------------------------------------

/* The contents of a selection are returned in a GtkSelectionData
   structure. selection/target identify the request.
   type specifies the type of the return; if length < 0, and
   the data should be ignored. This structure has object semantics -
   no fields should be modified directly, they should not be created
   directly, and pointers to them should not be stored beyond the duration of
   a callback. (If the last is changed, we'll need to add reference
   counting)

struct _GtkSelectionData
{
  GdkAtom selection;
  GdkAtom target;
  GdkAtom type;
  gint    format;
  guchar *data;
  gint    length;
};

*/

//-----------------------------------------------------------------------------
// "selection_received" for targets
//-----------------------------------------------------------------------------

extern "C" {
static void
targets_selection_received( GtkWidget *WXUNUSED(widget),
                            GtkSelectionData *selection_data,
                            guint32 WXUNUSED(time),
                            wxClipboardGtk *clipboard )
{
    if ( wxTheClipboard && selection_data->length > 0 )
    {
        // make sure we got the data in the correct form
        GdkAtom type = selection_data->type;
        if ( type != GDK_SELECTION_TYPE_ATOM )
        {
            if ( strcmp(gdk_atom_name(type), "TARGETS") )
            {
                wxLogTrace( TRACE_CLIPBOARD,
                            _T("got unsupported clipboard target") );

                clipboard->m_waiting = FALSE;
                return;
            }
        }

#ifdef __WXDEBUG__
        wxDataFormat clip( selection_data->selection );
        wxLogTrace( TRACE_CLIPBOARD,
                    wxT("selection received for targets, clipboard %s"),
                    clip.GetId().c_str() );
#endif // __WXDEBUG__

        // the atoms we received, holding a list of targets (= formats)
        GdkAtom *atoms = (GdkAtom *)selection_data->data;

        for (unsigned int i=0; i<selection_data->length/sizeof(GdkAtom); i++)
        {
            wxDataFormat format( atoms[i] );

            wxLogTrace( TRACE_CLIPBOARD,
                        wxT("selection received for targets, format %s"),
                        format.GetId().c_str() );

//            printf( "format %s requested %s\n",
//                    gdk_atom_name( atoms[i] ),
//                    gdk_atom_name( clipboard->m_targetRequested ) );

            if (format == clipboard->m_targetRequested)
            {
                clipboard->m_waiting = FALSE;
                clipboard->m_formatSupported = TRUE;
                return;
            }
        }
    }

    clipboard->m_waiting = FALSE;
}
}

//-----------------------------------------------------------------------------
// "selection_received" for the actual data
//-----------------------------------------------------------------------------

extern "C" {
static void
selection_received( GtkWidget *WXUNUSED(widget),
                    GtkSelectionData *selection_data,
                    guint32 WXUNUSED(time),
                    wxClipboardGtk *clipboard )
{
    if (!wxTheClipboard)
    {
        clipboard->m_waiting = FALSE;
        return;
    }

    wxDataObject *data_object = clipboard->m_receivedData;

    if (!data_object)
    {
        clipboard->m_waiting = FALSE;
        return;
    }

    if (selection_data->length <= 0)
    {
        clipboard->m_waiting = FALSE;
        return;
    }

    wxDataFormat format( selection_data->target );

    // make sure we got the data in the correct format
    if (!data_object->IsSupportedFormat( format ) )
    {
        clipboard->m_waiting = FALSE;
        return;
    }

#if 0
    This seems to cause problems somehow
    // make sure we got the data in the correct form (selection type).
    // if so, copy data to target object
    if (selection_data->type != GDK_SELECTION_TYPE_STRING)
    {
        clipboard->m_waiting = FALSE;
        return;
    }
#endif

    data_object->SetData( format, (size_t) selection_data->length, (const char*) selection_data->data );

    wxTheClipboard->m_formatSupported = TRUE;
    clipboard->m_waiting = FALSE;
}
}

//-----------------------------------------------------------------------------
// "selection_clear"
//-----------------------------------------------------------------------------

extern "C" {
static gint
selection_clear_clip( GtkWidget *WXUNUSED(widget), GdkEventSelection *event )
{
    if (!wxTheClipboard) return TRUE;

    if (event->selection == GDK_SELECTION_PRIMARY)
    {
        wxTheClipboard->m_ownsPrimarySelection = FALSE;

//madedit
        if (wxTheClipboard->m_primarySelectionData)
        {
            wxLogTrace(TRACE_CLIPBOARD, wxT("Primary selection will get cleared" ));

            delete wxTheClipboard->m_primarySelectionData;
            wxTheClipboard->m_primarySelectionData = (wxDataObject*) NULL;
        }
//madedit

    }
    else
    if (event->selection == g_clipboardAtom)
    {
        wxTheClipboard->m_ownsClipboard = FALSE;

//madedit
        if (wxTheClipboard->m_clipboardData)
        {
            wxLogTrace(TRACE_CLIPBOARD, wxT("Clipboard will get cleared" ));

            delete wxTheClipboard->m_clipboardData;
            wxTheClipboard->m_clipboardData = (wxDataObject*)NULL;
        }
//madedit

    }
    else
    {
        wxTheClipboard->m_waiting = FALSE;
        return FALSE;
    }

#if 0
//madedit
    if ((!wxTheClipboard->m_ownsPrimarySelection) &&
        (!wxTheClipboard->m_ownsClipboard))
    {
        /* the clipboard is no longer in our hands. we can the delete clipboard data. */
        if (wxTheClipboard->m_data)
        {
            wxLogTrace(TRACE_CLIPBOARD, wxT("wxClipboard will get cleared" ));

            delete wxTheClipboard->m_data;
            wxTheClipboard->m_data = (wxDataObject*) NULL;
        }
    }
#endif

    wxTheClipboard->m_waiting = FALSE;
    return TRUE;
}
}

//-----------------------------------------------------------------------------
// selection handler for supplying data
//-----------------------------------------------------------------------------

extern "C" {
static void
selection_handler( GtkWidget *WXUNUSED(widget),
                   GtkSelectionData *selection_data,
                   guint WXUNUSED(info),
                   guint WXUNUSED(time),
                   gpointer WXUNUSED(data) )
{
    if (!wxTheClipboard) return;

    
    
//madedit    if (!wxTheClipboard->m_data) return;
    wxDataObject *data;

//madedit    wxDataObject *data = wxTheClipboard->m_data;
    if (selection_data->selection == GDK_SELECTION_PRIMARY)
        data = wxTheClipboard->m_primarySelectionData;
    else
        data = wxTheClipboard->m_clipboardData;

    if (!data) return;
//madedit

    wxDataFormat format( selection_data->target );

#ifdef __WXDEBUG__
    wxLogTrace(TRACE_CLIPBOARD,
               _T("clipboard data in format %s, GtkSelectionData is target=%s type=%s selection=%s"),
               format.GetId().c_str(),
               wxString::FromAscii(gdk_atom_name(selection_data->target)).c_str(),
               wxString::FromAscii(gdk_atom_name(selection_data->type)).c_str(),
               wxString::FromAscii(gdk_atom_name(selection_data->selection)).c_str()
               );
#endif

    if (!data->IsSupportedFormat( format )) return;

    int size = data->GetDataSize( format );

    if (size == 0) return;

    void *d = malloc(size);

    // Text data will be in UTF8 in Unicode mode.
    data->GetDataHere( selection_data->target, d );

    // NB: GTK+ requires special treatment of UTF8_STRING data, the text
    //     would show as UTF-8 data interpreted as latin1 (?) in other
    //     GTK+ apps if we used gtk_selection_data_set()
    if (format == wxDataFormat(wxDF_UNICODETEXT))
    {
        gtk_selection_data_set_text(
            selection_data,
            (const gchar*)d,
            size-1 );
    }
    else
    {
        gtk_selection_data_set(
            selection_data,
            GDK_SELECTION_TYPE_STRING,
            8*sizeof(gchar),
            (unsigned char*) d,
            size-1 );
    }

    free(d);
}
}

//-----------------------------------------------------------------------------
// wxClipboardGtk
//-----------------------------------------------------------------------------

IMPLEMENT_DYNAMIC_CLASS(wxClipboardGtk,wxObject)

wxClipboardGtk::wxClipboardGtk()
{
    m_open = FALSE;
    m_waiting = FALSE;

    m_ownsClipboard = FALSE;
    m_ownsPrimarySelection = FALSE;

//madedit   m_data = (wxDataObject*) NULL;
    m_primarySelectionData = (wxDataObject*) NULL;
    m_clipboardData = (wxDataObject*) NULL;

    m_receivedData = (wxDataObject*) NULL;

    /* we use m_targetsWidget to query what formats are available */

    m_targetsWidget = gtk_window_new( GTK_WINDOW_POPUP );
    gtk_widget_realize( m_targetsWidget );

    g_signal_connect (m_targetsWidget, "selection_received",
                      G_CALLBACK (targets_selection_received), this);

    /* we use m_clipboardWidget to get and to offer data */

    m_clipboardWidget = gtk_window_new( GTK_WINDOW_POPUP );
    gtk_widget_realize( m_clipboardWidget );

    g_signal_connect (m_clipboardWidget, "selection_received",
                      G_CALLBACK (selection_received), this);

    g_signal_connect (m_clipboardWidget, "selection_clear_event",
                      G_CALLBACK (selection_clear_clip), NULL);

    if (!g_clipboardAtom) g_clipboardAtom = gdk_atom_intern( "CLIPBOARD", FALSE );
    if (!g_targetsAtom) g_targetsAtom = gdk_atom_intern ("TARGETS", FALSE);

    m_formatSupported = FALSE;
    m_targetRequested = 0;

    m_usePrimary = FALSE;
}

wxClipboardGtk::~wxClipboardGtk()
{
    m_usePrimary = false;
    Clear();
    m_usePrimary = true;
    Clear();

    if (m_clipboardWidget) gtk_widget_destroy( m_clipboardWidget );
    if (m_targetsWidget) gtk_widget_destroy( m_targetsWidget );
}

void wxClipboardGtk::Clear()
{
//madedit    if (m_data)
    if (((m_usePrimary) && (m_primarySelectionData)) ||
            ((!m_usePrimary) && (m_clipboardData)))

    {
#if wxUSE_THREADS
        /* disable GUI threads */
#endif

        //  As we have data we also own the clipboard. Once we no longer own
        //  it, clear_selection is called which will set m_data to zero

//madedit        if (gdk_selection_owner_get( g_clipboardAtom ) == m_clipboardWidget->window)
        // it, clear_selection is called which will set m_clipboardData or
        // m_primarySelectionData to zero.
        if ((m_usePrimary) && (m_primarySelectionData))
        {
            if (gdk_selection_owner_get( GDK_SELECTION_PRIMARY ) == m_clipboardWidget->window)

            {
                m_waiting = TRUE;

//madedit            gtk_selection_owner_set( (GtkWidget*) NULL, g_clipboardAtom,
                gtk_selection_owner_set( (GtkWidget*) NULL, GDK_SELECTION_PRIMARY,
                                     (guint32) GDK_CURRENT_TIME );

                while (m_waiting) gtk_main_iteration();
            }

//madedit        if (gdk_selection_owner_get( GDK_SELECTION_PRIMARY ) == m_clipboardWidget->window)
            delete m_primarySelectionData;
            m_primarySelectionData = (wxDataObject*) NULL;
        }

        if ((!m_usePrimary) && (m_clipboardData))
        {
            if (gdk_selection_owner_get( g_clipboardAtom ) == m_clipboardWidget->window)
            {
                m_waiting = TRUE;

//madedit            gtk_selection_owner_set( (GtkWidget*) NULL, GDK_SELECTION_PRIMARY,
                gtk_selection_owner_set( (GtkWidget*) NULL, g_clipboardAtom,
                                     (guint32) GDK_CURRENT_TIME );

                while (m_waiting) gtk_main_iteration();
            }

        //if (m_data)
        //{
            //delete m_data;
            //m_data = (wxDataObject*) NULL;
            delete m_clipboardData;
            m_clipboardData = (wxDataObject*) NULL;
        }

#if wxUSE_THREADS
        /* re-enable GUI threads */
#endif
    }

    m_targetRequested = 0;
    m_formatSupported = FALSE;
}

bool wxClipboardGtk::Open()
{
    wxCHECK_MSG( !m_open, FALSE, wxT("clipboard already open") );

    m_open = TRUE;

    return TRUE;
}

bool wxClipboardGtk::SetData( wxDataObject *data )
{
    wxCHECK_MSG( m_open, FALSE, wxT("clipboard not open") );

    wxCHECK_MSG( data, FALSE, wxT("data is invalid") );

    Clear();

    return AddData( data );
}

bool wxClipboardGtk::AddData( wxDataObject *data )
{
    wxCHECK_MSG( m_open, FALSE, wxT("clipboard not open") );

    wxCHECK_MSG( data, FALSE, wxT("data is invalid") );

    // we can only store one wxDataObject
    Clear();

//madedit    m_data = data;

    // get formats from wxDataObjects
//madedit    wxDataFormat *array = new wxDataFormat[ m_data->GetFormatCount() ];
//madedit    m_data->GetAllFormats( array );
    wxDataFormat *array = new wxDataFormat[ data->GetFormatCount() ];
    data->GetAllFormats( array );

    // primary selection or clipboard
    GdkAtom clipboard = m_usePrimary ? (GdkAtom)GDK_SELECTION_PRIMARY
                                     : g_clipboardAtom;


//madedit    for (size_t i = 0; i < m_data->GetFormatCount(); i++)
    for (size_t i = 0; i < data->GetFormatCount(); i++)
    {
        wxLogTrace( TRACE_CLIPBOARD,
                    wxT("wxClipboardGtk now supports atom %s"),
                    array[i].GetId().c_str() );

//        printf( "added %s\n",
//                    gdk_atom_name( array[i].GetFormatId() ) );

        gtk_selection_add_target( GTK_WIDGET(m_clipboardWidget),
                                  clipboard,
                                  array[i],
                                  0 );  /* what is info ? */
    }

    delete[] array;

    g_signal_connect (m_clipboardWidget, "selection_get",
                      G_CALLBACK (selection_handler), NULL);

#if wxUSE_THREADS
    /* disable GUI threads */
#endif

    /* Tell the world we offer clipboard data */
    bool res = (gtk_selection_owner_set( m_clipboardWidget,
                                         clipboard,
                                         (guint32) GDK_CURRENT_TIME ));

    if (m_usePrimary)
    {
        m_primarySelectionData = data;
        m_ownsPrimarySelection = res;
    }
    else
    {
        m_clipboardData = data;
        m_ownsClipboard = res;
    }

#if wxUSE_THREADS
    /* re-enable GUI threads */
#endif

    return res;
}

void wxClipboardGtk::Close()
{
    wxCHECK_RET( m_open, wxT("clipboard not open") );

    m_open = FALSE;
}

bool wxClipboardGtk::IsOpened() const
{
    return m_open;
}

bool wxClipboardGtk::IsSupported( const wxDataFormat& format )
{
    /* reentrance problems */
    if (m_waiting) return FALSE;

    /* store requested format to be asked for by callbacks */
    m_targetRequested = format;

    wxLogTrace( TRACE_CLIPBOARD,
                wxT("wxClipboardGtk:IsSupported: requested format: %s"),
                format.GetId().c_str() );

    wxCHECK_MSG( m_targetRequested, FALSE, wxT("invalid clipboard format") );

    m_formatSupported = FALSE;

    /* perform query. this will set m_formatSupported to
       TRUE if m_targetRequested is supported.
       also, we have to wait for the "answer" from the
       clipboard owner which is an asynchronous process.
       therefore we set m_waiting = TRUE here and wait
       until the callback "targets_selection_received"
       sets it to FALSE */

    m_waiting = TRUE;

    gtk_selection_convert( m_targetsWidget,
                           m_usePrimary ? (GdkAtom)GDK_SELECTION_PRIMARY
                                        : g_clipboardAtom,
                           g_targetsAtom,
                           (guint32) GDK_CURRENT_TIME );

    while (m_waiting) gtk_main_iteration();

#if defined(__WXGTK20__) && wxUSE_UNICODE
    if (!m_formatSupported && format == wxDataFormat(wxDF_UNICODETEXT))
    {
        // Another try with plain STRING format
        extern GdkAtom g_altTextAtom;
        return IsSupported(g_altTextAtom);
    }
#endif

    return m_formatSupported;
}

bool wxClipboardGtk::GetData( wxDataObject& data )
{
    wxCHECK_MSG( m_open, FALSE, wxT("clipboard not open") );

    /* get formats from wxDataObjects */
    wxDataFormat *array = new wxDataFormat[ data.GetFormatCount() ];
    data.GetAllFormats( array );

    for (size_t i = 0; i < data.GetFormatCount(); i++)
    {
        wxDataFormat format( array[i] );

        wxLogTrace( TRACE_CLIPBOARD,
                    wxT("wxClipboardGtk::GetData: requested format: %s"),
                    format.GetId().c_str() );

        /* is data supported by clipboard ? */

        /* store requested format to be asked for by callbacks */
        m_targetRequested = format;

        wxCHECK_MSG( m_targetRequested, FALSE, wxT("invalid clipboard format") );

        m_formatSupported = FALSE;

       /* perform query. this will set m_formatSupported to
          TRUE if m_targetRequested is supported.
          also, we have to wait for the "answer" from the
          clipboard owner which is an asynchronous process.
          therefore we set m_waiting = TRUE here and wait
          until the callback "targets_selection_received"
          sets it to FALSE */

        m_waiting = TRUE;

        gtk_selection_convert( m_targetsWidget,
                           m_usePrimary ? (GdkAtom)GDK_SELECTION_PRIMARY
                                        : g_clipboardAtom,
                           g_targetsAtom,
                           (guint32) GDK_CURRENT_TIME );

        while (m_waiting) gtk_main_iteration();

        if (!m_formatSupported) continue;

        /* store pointer to data object to be filled up by callbacks */
        m_receivedData = &data;

        /* store requested format to be asked for by callbacks */
        m_targetRequested = format;

        wxCHECK_MSG( m_targetRequested, FALSE, wxT("invalid clipboard format") );

        /* start query */
        m_formatSupported = FALSE;

        /* ask for clipboard contents.  this will set
           m_formatSupported to TRUE if m_targetRequested
           is supported.
           also, we have to wait for the "answer" from the
           clipboard owner which is an asynchronous process.
           therefore we set m_waiting = TRUE here and wait
           until the callback "targets_selection_received"
           sets it to FALSE */

        m_waiting = TRUE;

        wxLogTrace( TRACE_CLIPBOARD,
                    wxT("wxClipboardGtk::GetData: format found, start convert") );

        gtk_selection_convert( m_clipboardWidget,
                               m_usePrimary ? (GdkAtom)GDK_SELECTION_PRIMARY
                                            : g_clipboardAtom,
                               m_targetRequested,
                               (guint32) GDK_CURRENT_TIME );

        while (m_waiting) gtk_main_iteration();

        /* this is a true error as we checked for the presence of such data before */
        wxCHECK_MSG( m_formatSupported, FALSE, wxT("error retrieving data from clipboard") );

        /* return success */
        delete[] array;
        return TRUE;
    }

    wxLogTrace( TRACE_CLIPBOARD,
                wxT("wxClipboardGtk::GetData: format not found") );

    /* return failure */
    delete[] array;
    return FALSE;
}


wxClipboardGtk *g_ClipboardGtk=NULL;
wxClipboardGtk *GetClipboardGtk()
{
    if(g_ClipboardGtk==NULL)
    {
        g_ClipboardGtk=new wxClipboardGtk;
    }
    return g_ClipboardGtk;
}



#endif
  // wxUSE_CLIPBOARD

#endif
  // __WXGTK__

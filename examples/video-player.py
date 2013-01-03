'''
A simple GUI for playing TS streams.
Prticularily this scrip was devised to evaluate/demonstrate timeshifting.

It uses GObject introspection so the assumption is that GStreamer-1.0
introspection files are generated and made available to the Python process.
For example if Gstreamer-1.0 is installed in $HOME/opt/gstreamer-1.0,
the following variables should be available:

  LD_LIBRARY_PATH=$HOME/opt/gstreamer-1.0/lib:$LD_LIBRARY_PATH
  GI_TYPELIB_PATH=$HOME/opt/gstreamer-1.0/lib/girepository-1.0

'''

from os import path
import sys

import gi
gi.require_version('Gst', '1.0')
from gi.repository import GObject, Gtk, Gst

# Needed for window.get_xid(), xvimagesink.set_window_handle(), respectively:
from gi.repository import GdkX11, GstVideo


GObject.threads_init()
Gst.init(None)

class Player(object):
    def __init__(self):
        self.position = Gst.CLOCK_TIME_NONE
        self.duration = Gst.CLOCK_TIME_NONE
        self.pcr_configured = False
        self.is_recording = False
        self.changed_id = -1

        self.window = Gtk.Window()
        self.window.connect('destroy', self.quit)
        self.window.set_default_size(800, 450)

        box = Gtk.Box()
        box.set_spacing (5)
        box.set_orientation(Gtk.Orientation.VERTICAL)
        self.window.add(box)

        self.drawingarea = Gtk.DrawingArea()
        box.pack_start(self.drawingarea, True, True, 0)

        hbox = Gtk.Box()
        hbox.set_spacing (5)
        hbox.set_orientation(Gtk.Orientation.HORIZONTAL)

        self.stop_button = Gtk.Button(label='Stop')
        def stop_button_press_cb(widget, event):
            self.seek_end()
        self.stop_button.connect('button-press-event', stop_button_press_cb)

        hbox.pack_start(self.stop_button, False, False, 0)

        self.adjustment = Gtk.Adjustment(0.0, 0.00, 100.0, 0.1, 1.0, 1.0)
        self.scale = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, adjustment=self.adjustment)
        self.scale.set_digits(0)
        self.scale.set_hexpand(True)
        self.scale.set_valign(Gtk.Align.START)
        self.scale.connect('button-press-event', self.scale_button_press_cb)
        self.scale.connect('button-release-event', self.scale_button_release_cb)
        self.scale.connect('format-value', self.scale_format_value_cb)

        hbox.pack_start(self.scale, False, True, 0)

        box.pack_start(hbox, False, False, 0)

        # Create GStreamer pipeline
        self.pipeline = Gst.Pipeline()

        # Create bus to get events from GStreamer pipeline
        self.bus = self.pipeline.get_bus()
        self.bus.add_signal_watch()
        self.bus.connect('message::eos', self.on_eos)
        self.bus.connect('message::error', self.on_error)

        # This is needed to make the video output in our DrawingArea:
        self.bus.enable_sync_message_emission()
        self.bus.connect('sync-message::element', self.on_sync_message)

        # Create GStreamer elements
        if 1 == len(sys.argv):
            '''
            Example stream (BBC One) can be generated as follows:

            gst-launch-0.10 -v \
              dvbsrc \
                bandwidth=8 code-rate-lp=NONE code-rate-hp=2/3 guard=32 \
                hierarchy=NONE modulation="QAM 64" trans-mode=8k \
                inversion=AUTO frequency=490000000 pids=100:101 symbol-rate=27500 \
              ! queue ! udpsink host=<target IP> port=10000

            '''
            src = 'udpsrc port=10000 caps="video/mpegts, media=(string)video, encoding-name=(string)MP2T-ES"'
        else:
            src = 'souphttpsrc is-live=true location=%s' % (sys.argv[1])

        self.playbin = Gst.parse_bin_from_description(
            '%s' \
            ' ! queue ' \
            ' ! flumpegshifterbin name=timeshifter' \
                ' cache-size=128000000 temp-template=/tmp/timeshifter-XXXXXX' \
            ' ! decodebin ! autovideosink' % (src),
            False);
        self.pipeline.add(self.playbin)

        self.update_id = GObject.timeout_add(1000, self.update_scale_cb)
    
    def update_scale_cb(self):
        self.position, self.duration = self.query_position()
        print "pos: %i, dur: %i" % (self.position, self.duration)
        if Gst.CLOCK_TIME_NONE != self.position and 0 != self.duration:
            value = self.position * 100.0 / self.duration
            self.adjustment.set_value(value)

        '''
        # a work-around for the timeshifter failing to recycle cache slots 
        if not self.is_recording:
            structure = Gst.Structure.new_empty("shifter-start-recording")
            event = Gst.Event.new_custom(Gst.EventType.CUSTOM_UPSTREAM, structure)
            if self.pipeline.send_event(event):
                print "start recording"
                self.is_recording = True
            else:
                print "start recording failed"
        '''
        return True
    
    def query_position(self):
        try:
            format, position = self.pipeline.query_position(Gst.Format.TIME)
        except:
            position = Gst.CLOCK_TIME_NONE

        try:
            format, duration = self.pipeline.query_duration(Gst.Format.TIME)
        except:
            duration = Gst.CLOCK_TIME_NONE

        return (position, duration)

    def scale_format_value_cb(self, scale, value):
        if Gst.CLOCK_TIME_NONE == self.duration:
            real = 0
        else:
            real = value * self.duration / 100

        seconds = real / Gst.SECOND

        return '%02d:%02d' % (seconds / 60, seconds % 60)

    def run(self):
        self.window.show_all()
        # You need to get the XID after window.show_all().  You shouldn't get it
        # in the on_sync_message() handler because threading issues will cause
        # segfaults there.
        self.xid = self.drawingarea.get_property('window').get_xid()
        self.pipeline.set_state(Gst.State.PLAYING)

        Gtk.main()

    def quit(self, window):
        self.pipeline.set_state(Gst.State.NULL)
        Gtk.main_quit()

    def on_sync_message(self, bus, msg):
        s = msg.get_structure()

        if s.get_name() == 'prepare-window-handle':
            print('prepare-window-handle')
            msg.src.set_window_handle(self.xid)
        
    def seek(self, location):
        #print 'seeking to %r' % location
        res = self.pipeline.seek(1.0, Gst.Format.TIME,
            Gst.SeekFlags.FLUSH | Gst.SeekFlags.ACCURATE,
            Gst.SeekType.SET, location,
            Gst.SeekType.NONE, 0)

        #if res:
        #    print 'setting new stream time to 0'
        #else:
        #    print 'seek to %r failed' % location

    def seek_end(self):
        res = self.pipeline.seek(1.0, Gst.Format.TIME,
            Gst.SeekFlags.FLUSH | Gst.SeekFlags.ACCURATE,
            Gst.SeekType.END, -1,
            Gst.SeekType.NONE, 0)

        #if res:
        #    print 'setting new stream time to 0'
        #else:
        #    print 'seek to end failed'

    def scale_button_press_cb(self, widget, event):
        #print 'starting seek'

        # don't timeout-update position during seek
        if self.update_id != -1:
            GObject.source_remove(self.update_id)
            self.update_id = -1

        # make sure we get changed notifies
        if self.changed_id == -1:
            self.changed_id = self.scale.connect('value-changed',
                self.scale_value_changed_cb)

    def scale_value_changed_cb(self, scale):
        real = long(scale.get_value() * self.duration / 100) # in ns
        #print 'value changed, perform seek to %r' % real
        self.seek(real)
        # allow for a preroll
        self.pipeline.get_state(timeout = 50 * Gst.MSECOND)

    def scale_button_release_cb(self, widget, event):
        widget.disconnect(self.changed_id)
        self.changed_id = -1

        if self.update_id != -1:
            self.error('Had a previous update timeout id')
        else:
            self.update_id = GObject.timeout_add(1000, self.update_scale_cb)

    def on_eos(self, bus, msg):
        print('on_eos(): seeking to start of video')
        self.pipeline.seek_simple(
            Gst.Format.TIME,        
            Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
            0)

    def on_error(self, bus, msg):
        print('on_error():', msg.parse_error())


p = Player()
p.run()


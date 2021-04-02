<?xml version="1.0" encoding="UTF-8"?>
<databrowser>
  <title></title>
  <show_toolbar>true</show_toolbar>
  <update_period>3.0</update_period>
  <scroll_step>5</scroll_step>
  <scroll>true</scroll>
  <start>-10 minutes</start>
  <end>now</end>
  <archive_rescale>STAGGER</archive_rescale>
  <foreground>
    <red>0</red>
    <green>0</green>
    <blue>0</blue>
  </foreground>
  <background>
    <red>255</red>
    <green>255</green>
    <blue>255</blue>
  </background>
  <title_font>Liberation Sans|20|1</title_font>
  <label_font>Liberation Sans|14|1</label_font>
  <scale_font>Liberation Sans|12|0</scale_font>
  <legend_font>Liberation Sans|14|0</legend_font>
  <axes>
    <axis>
      <visible>true</visible>
      <name>precent</name>
      <use_axis_name>false</use_axis_name>
      <use_trace_names>true</use_trace_names>
      <right>false</right>
      <color>
        <red>0</red>
        <green>0</green>
        <blue>0</blue>
      </color>
      <min>-1.0</min>
      <max>101.0</max>
      <grid>false</grid>
      <autoscale>false</autoscale>
      <log_scale>false</log_scale>
    </axis>
    <axis>
      <visible>true</visible>
      <name>Size</name>
      <use_axis_name>false</use_axis_name>
      <use_trace_names>true</use_trace_names>
      <right>false</right>
      <color>
        <red>0</red>
        <green>0</green>
        <blue>0</blue>
      </color>
      <min>-1.0</min>
      <max>20000.0</max>
      <grid>false</grid>
      <autoscale>false</autoscale>
      <log_scale>false</log_scale>
    </axis>
    <axis>
      <visible>true</visible>
      <name>MB/s</name>
      <use_axis_name>false</use_axis_name>
      <use_trace_names>true</use_trace_names>
      <right>false</right>
      <color>
        <red>0</red>
        <green>0</green>
        <blue>0</blue>
      </color>
      <min>150.0</min>
      <max>300.0</max>
      <grid>false</grid>
      <autoscale>false</autoscale>
      <log_scale>false</log_scale>
    </axis>
    <axis>
      <visible>true</visible>
      <name>Hz</name>
      <use_axis_name>false</use_axis_name>
      <use_trace_names>true</use_trace_names>
      <right>false</right>
      <color>
        <red>0</red>
        <green>0</green>
        <blue>0</blue>
      </color>
      <min>-0.1</min>
      <max>10.0</max>
      <grid>false</grid>
      <autoscale>false</autoscale>
      <log_scale>false</log_scale>
    </axis>
  </axes>
  <annotations>
  </annotations>
  <pvlist>
    <pv>
      <display_name>File Write</display_name>
      <visible>true</visible>
      <name>TST:WrtRate-I</name>
      <axis>2</axis>
      <color>
        <red>127</red>
        <green>0</green>
        <blue>255</blue>
      </color>
      <trace_type>AREA</trace_type>
      <linewidth>2</linewidth>
      <line_style>SOLID</line_style>
      <point_type>NONE</point_type>
      <point_size>2</point_size>
      <waveform_index>0</waveform_index>
      <period>0.0</period>
      <ring_size>50000</ring_size>
      <request>OPTIMIZED</request>
    </pv>
    <pv>
      <display_name>File Size</display_name>
      <visible>true</visible>
      <name>TST:LstSz-I</name>
      <axis>1</axis>
      <color>
        <red>255</red>
        <green>127</green>
        <blue>0</blue>
      </color>
      <trace_type>AREA</trace_type>
      <linewidth>2</linewidth>
      <line_style>SOLID</line_style>
      <point_type>NONE</point_type>
      <point_size>2</point_size>
      <waveform_index>0</waveform_index>
      <period>0.0</period>
      <ring_size>50000</ring_size>
      <request>OPTIMIZED</request>
    </pv>
    <pv>
      <display_name>Buf Writing</display_name>
      <visible>true</visible>
      <name>TST:PWrt-I</name>
      <axis>0</axis>
      <color>
        <red>0</red>
        <green>0</green>
        <blue>255</blue>
      </color>
      <trace_type>AREA</trace_type>
      <linewidth>2</linewidth>
      <line_style>SOLID</line_style>
      <point_type>NONE</point_type>
      <point_size>2</point_size>
      <waveform_index>0</waveform_index>
      <period>0.0</period>
      <ring_size>50000</ring_size>
      <request>OPTIMIZED</request>
    </pv>
    <pv>
      <display_name>Net RX</display_name>
      <visible>true</visible>
      <name>TST:RXBRate-I</name>
      <axis>2</axis>
      <color>
        <red>0</red>
        <green>255</green>
        <blue>127</blue>
      </color>
      <trace_type>AREA</trace_type>
      <linewidth>2</linewidth>
      <line_style>SOLID</line_style>
      <point_type>NONE</point_type>
      <point_size>2</point_size>
      <waveform_index>0</waveform_index>
      <period>0.0</period>
      <ring_size>50000</ring_size>
      <request>OPTIMIZED</request>
    </pv>
    <pv>
      <display_name>Buf Ready</display_name>
      <visible>true</visible>
      <name>TST:PRXe-I</name>
      <axis>0</axis>
      <color>
        <red>102</red>
        <green>153</green>
        <blue>102</blue>
      </color>
      <trace_type>AREA</trace_type>
      <linewidth>2</linewidth>
      <line_style>SOLID</line_style>
      <point_type>NONE</point_type>
      <point_size>2</point_size>
      <waveform_index>0</waveform_index>
      <period>0.0</period>
      <ring_size>50000</ring_size>
      <request>OPTIMIZED</request>
    </pv>
    <pv>
      <display_name>Buf Free</display_name>
      <visible>true</visible>
      <name>TST:PFree-I</name>
      <axis>0</axis>
      <color>
        <red>255</red>
        <green>0</green>
        <blue>0</blue>
      </color>
      <trace_type>AREA</trace_type>
      <linewidth>2</linewidth>
      <line_style>SOLID</line_style>
      <point_type>NONE</point_type>
      <point_size>2</point_size>
      <waveform_index>0</waveform_index>
      <period>0.0</period>
      <ring_size>50000</ring_size>
      <request>OPTIMIZED</request>
    </pv>
    <pv>
      <display_name>TST:DrpRate-I</display_name>
      <visible>true</visible>
      <name>TST:DrpRate-I</name>
      <axis>3</axis>
      <color>
        <red>0</red>
        <green>255</green>
        <blue>0</blue>
      </color>
      <trace_type>AREA</trace_type>
      <linewidth>2</linewidth>
      <line_style>SOLID</line_style>
      <point_type>NONE</point_type>
      <point_size>2</point_size>
      <waveform_index>0</waveform_index>
      <period>0.0</period>
      <ring_size>50000</ring_size>
      <request>OPTIMIZED</request>
    </pv>
  </pvlist>
</databrowser>

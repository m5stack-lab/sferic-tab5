idf_component_register(
    SRCS "spike_cam.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_video
        esp_io_expander
        esp_io_expander_pi4ioe5v6408
)

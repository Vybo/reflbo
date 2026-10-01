# The map under both radars (spec §11.1): web-Mercator views, the built-in borders, towns and
# airports, and their drawing. Pure C: also built on the host.
idf_component_register(SRCS "map_view.c"
                       INCLUDE_DIRS "include")

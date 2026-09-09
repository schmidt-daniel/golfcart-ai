IMPORTANT: This is a draft. Don't 


# General layout

Screen is in portrait mode, 480x640 pixels

+--------------------+
| [TIME]    [STATUS] |
+--------------------+
|                    |
|                    |
|                    |
|                    |
|     [CONTENT]      |
|                    |
|                    |
|                    |
|                    |
|                    |
+--------------------+

Status bar:
[TIME]: Current time
[STATUS]: Icons representing the status of the system
The status bar is always shown.

Content:
[CONTENT]: Menu or a feature-specific view, e.g. hole layout, sensor debug data, configuration screen

Status icons:
* GPS quality: number of satellites, accuracy
* Current operating mode: Manual/push-assist, follow-me, teleop, autonomous
* Battery: state of charge

# General joystick actions
[MOVE UP/DOWN/RIGHT/LEFT]: Move the joystick in the cardinal directions. Usually used to highlight or move an element. When activated for more than 1 second, it should repeat the movement until released.
[PRESS]: Pressed down the joystick. Usually used to select or change an element.
[LONG]: Longpress (>=3 secs) of the joystick button. Ususally used to go to the main view.
[DOUBLE]: Double-press (2 presses within 1.5 seconds) of the joystick button. Usually go go back to the previous screen.


# Main Menu

## Content
+--------------------+
| Main Menu          |
+--------------------+
| MAP                |
| MODE               |
| ASSIST             |
| CHANGE HOLE        |
| SELECT COURSE      |
| DEBUG              |
| SHUTDOWN           |
|                    |
+--------------------+

MAP: Go to map view of the current hole, only selectable when a course and teebox has been selected.
MODE: Go to the mode selection view.
ASSIST: Go to the assist configuration view.
CHANGE HOLE: Go to the hole selection view, only selectable when a course and teebox has been selected.
SELECT COURSE: Go to the course selection view.
DEBUG: Go to the debug view.
SHUTDOWN: Show a confirmation dialog whether the user really wants to shutdown the system. Do so if confirmed by the user.

## Actions
[MOVE UP/DOWN]: Highlights a menu item from the list
[PRESS]: Selects an item from the menu and jumps to the respective view
[DOUBLE]: Jumps to the map view if course and teebox have been selected.


# Course selection view

This screen is shown when the HMI first loads. It will allow the user to directly to select the course they are playing on.

## Content
+--------------------+
| Courses            |
+--------------------+
|                    |
|                    |
|       [LIST]       |
|                    |
|                    |
|                    |
|                    |
|                    |
+--------------------+
[LIST]: A list of courses available on the device + "Main Menu".

## Actions
[MOVE UP/DOWN]: Highlight a course from the list. List might have to scroll.
[PRESS]: Selects a course from the list and jumps to the teebox selection screen. Jumps to main menu if pressed "Main Menu".
[DOUBLE]: Jumps to main menu.


# Teebox selection view

This screen is shown when the HMI first loads. It will allow the user to directly to select the course they are playing on.

## Content
+--------------------+
| Teeboxes           |
+--------------------+
|                    |
|                    |
|       [LIST]       |
|                    |
|                    |
|                    |
|                    |
|                    |
+--------------------+
[LIST]: A list of the teeboxes (colors or similar) available at this course + "Course Selection".

## Actions
[MOVE UP/DOWN]: Highlight a teebox from the list. List might have to scroll.
[PRESS]: Selects a teebox from the list and jumps to the map view screen. Jumps to course selection view if pressed "Course Selection".
[DOUBLE]: Jumps to main menu.


# Map view

Displays a map of the hole the user is currently playing with additional information.
Also allows to select a speed when in manual mode.

## Content
+--------------------+
|                    |
|                    |
|                    |
|                    |
|        [MAP]       |
|                    |
|                    |
|                    |
+--------------------+
| (-)   Speed   (+)  |
+--------------------+

[MAP]: A rendered, colorful bird's eye view of the current hole. Teebox at the bottom, green at the top.
Speed bar: Allows the user to select the speed when in manual/push-assist mode. Bar is not shown if user in not in manual/push-assist mode.

## Actions
[MOVE RIGHT/LEFT]: Decrease/increase speed for manual/push-assist mode
[PRESS]: Activates/deactivates push-assist
[DOUBLE]: Jumps to main menu

# Mode selection view

This view allows the user to select one of three operating modes. There is also a fourth (hidden) one: Teleop is automatically and temporarily activated when triggered from the WebApp.

## Content
+---------+----------+
|   Manual/Push Ass. |
| x Follow Me        |
|   Autonomous       |
|                    |
| Main Menu          |
|                    |
|                    |
|                    |
|                    |
|                    |
+--------------------+

Mode list: A list of the three modes a user can select. The currently selected option should feature a checkmark.
Main Menu: to return to the Main Menu.

## Actions
[MOVE UP/DOWN]: Highlight a list item.
[PRESS]: Select mode or return to main menu, respectively.
[DOUBLE]: Jumps to main menu.


# Assist view

# Change hole view

# Debug view

# System debug view
## Content
Battery charge
CPU load
Disk space

# GPS debug view

Shows information from the GPS system

## Content
Lat
Long
Ele
Number of satellites
Timestamp
Accuracy x/y
Accuracy ele


# Lidar debug view
## Content
Point cloud

# Camera debug view
## Content
Camera view
Segmentation

# IMU debug view
## Content
X/Y/Z rotation
X/Y/Z acceleration

# Navigation debug view
## Content
Map with costmap, features and planned route

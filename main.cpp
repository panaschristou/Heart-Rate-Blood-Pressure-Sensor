#include <mbed.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "drivers/LCD_DISCO_F429ZI.h"
// Import all the functions for working with the display
#define BACKGROUND 1
// The value that indicates the background layer, to be passed to 
// the LCD functions
#define FOREGROUND 0
// The value that indicates the foreground layer, to be passed to 
// the LCD functions
#define SENSOR_ADDR 0b0011000
// The sensor's 7-bit address

uint8_t OUTPUT_COMMAND[3] = {0xAA, 0x00, 0x00};
// The output measurement command to be sent to the sensor over I2C
static constexpr uint8_t read_addr = (SENSOR_ADDR << 1U) | 1U;
// The sensor' address for reading data
static constexpr uint8_t write_addr = SENSOR_ADDR << 1U;
// The sensor's address for writing data
static uint8_t read_buf[20];
// A buffer for storing the data received from the sensor over I2C
volatile uint8_t sensor_status;
// A global variable for storing the 8-bit sensor status
volatile uint32_t pressure_reading;
// A global variable for storing the sensor's 24-bit pressure reading
volatile int pressure;
// A global variable for storing the pressure calculated from the reading
volatile int restarted_after_timeout = 0;
// A global variable for tracking whether the program restarted because 
// deflating the air bag took more than 90 seconds
volatile int in_debug_mode = 0;
// A global variable for tracking whether the program is currently 
// in debug mode. 1 if it is; 0 otherwise.
volatile int heart_rate;
// A global variable for storing the heart rate
volatile int systolic;
// A global variable for storing the systolic blood pressure
volatile int diastolic;
// A global variable for storing the diastolic blood pressure
int * vals_1d = (int *) malloc(900 * sizeof(int));
int * osci = (int *) malloc(900 * sizeof(int));
// Allocate 2 arrays of 900 integers on the heap, which will be 
// used by calc_stats to analyze the pressure waves

I2C Wire(PC_9, PA_8);
// Declare an mbed I2C instance
// Use PC_9 for the SDA line and PA_8 for the SCL line
InterruptIn button_int(USER_BUTTON, PullDown);
// Create an InterruptIn connected to the user button and 
// configure the button as pull-down

LCD_DISCO_F429ZI lcd;
// Declare an instance for the LCD of the F429 microcontroller

// Below are forward declarations for the functions
void sleep_and_update_pressure();
void print_pressure_values(int, int, uint16_t *);
void timeout_restart();
void check_release_rate(int, uint16_t *, char *);
void calc_stats();
void calc_pressure(int);
void enter_operating_mode();
void wait_for_busy_flag();
void read_pressure();
void debug_mode();
void setup_lcd_background();
void setup_lcd_foreground();
void pump_up_to_150();
void sleep_and_update_pressure();
void open_valve();
void show_stats();
void button_isr();

void calc_pressure(int output) {
  int output_max = 3774873;
  int output_min = 419430;
  // According to Transfer Function B, 
  // the max equals 22.5% * (2 ^ 24), 
  // and the min. equals 2.5% * (2 ^ 24)
  int p_max = 300;
  int p_min = 0;
  // The pressure range of the sensor is 0 to 300

  pressure = (int) (output - output_min) * (p_max - p_min) / (output_max - output_min) + p_min;
  // The formula on the data sheet
}

void enter_operating_mode() {
  // Send the Output Measurement Command over I2C to make the sensor
  // exit Standby Mode and enter Operating Mode

  Wire.write(write_addr, (const char *) OUTPUT_COMMAND, 3, false);
  thread_sleep_for(10);
  // This function is called repeatedly, and 
  // at least 10 ms between calls is required 
  // for the sensor to function
}

void wait_for_busy_flag() {
  // To be called after the Output Measurement Command is sent
  // The pressure reading is not ready until the busy flag clears
  // This function blocks the read_pressure function from running until 
  // the busy flag clears
  // It assumes that the sensor is in Operating Mode before it's called

  Wire.read(read_addr, (char*)&read_buf[0], 1, false);
  // Read the status byte over I2C
  sensor_status = read_buf[0];
  // Update the status byte saved in the global variable
  uint8_t is_busy = sensor_status & 32U;
  // Bit 5 is the busy flag

  while (is_busy) {
    // Stay in this function while the sensor is busy
    enter_operating_mode();
    // Make the sensor enter Operating Mode
    Wire.read(read_addr, (char*)&read_buf[0], 1, false);
    // Read the status byte again
    sensor_status = read_buf[0];
    // Update the status byte saved in the global variable
    is_busy = sensor_status & 32U;
    // Update the local busy flag
  }
}

void read_pressure() {
  // Wait for the busy flag to clear and read the current pressure 
  // over I2C

  enter_operating_mode();
  // Make the sensor enter Opearating Mode
  wait_for_busy_flag();
  // Wait until the data is available
  enter_operating_mode();
  // The device entered Standy Mode after the previous transaction, 
  // so the command needs to be sent again to make it enter 
  // Operating Mode
  Wire.read(read_addr, (char*)&read_buf[0], 4, false);
  // Now the pressure reading is available. Read it and save it in 
  // the read buffer

  sensor_status = read_buf[0];
  // The first byte received is the status byte
  pressure_reading = read_buf[1] << 16U;
  pressure_reading |= read_buf[2] << 8U;
  pressure_reading |= read_buf[3];
  // The pressure reading is 24 bits
  // The most significant bits come first
  // Shift the bits accordingly and write them into the 
  // global variable

  calc_pressure((int) pressure_reading);
  // Convert the raw data to a value in mmHg
  // Store it in the global variable
}

void sleep_and_update_pressure() {
  // A function that changes the sleep duration once and for all so 
  // that you don't have to change the duration in every function 
  // that calls read_pressure

  thread_sleep_for(100);
  // Sleep for 100 milliseconds before updating the pressure
  read_pressure();
  // Update the pressure
}

void button_isr() {
  // Called when a button interrupt occurs
  // Flips the value of in_debug_mode

  in_debug_mode ^= 1U;
}

void debug_mode() {
  lcd.Clear(LCD_COLOR_BLACK);
  char buff[20][60];
  snprintf(buff[0], 60, "DEBUG MODE");
  snprintf(buff[1], 60, " ");
  snprintf(buff[2], 60, "The sensor is");
  snprintf(buff[4], 60, " ");
  snprintf(buff[5], 60, "Internal math ");
  snprintf(buff[6], 60, "saturation has ");
  snprintf(buff[8], 60, " ");
  snprintf(buff[9], 60, "The memory ");
  snprintf(buff[10], 60, "integrity test");
  snprintf(buff[12], 60, " ");
  snprintf(buff[13], 60, "The device is");
  snprintf(buff[16], 60, " ");
  snprintf(buff[17], 60, "Press the blue");
  snprintf(buff[18], 60, "button to exit");

  while (in_debug_mode) {
    uint8_t math_saturation = sensor_status & 1U;
    // The math saturation status is indicated by Bit 0 in the sensor status byte
    uint8_t integrity_test_passed = sensor_status & 4U;
    // The memory integrity test result is indicated by Bit 2
    uint8_t is_busy = sensor_status & 32U;
    // Bit 5 is the busy flag
    uint8_t is_powered = sensor_status & 64U;
    // Bit 6 indicates whether the sensor is powered
    if (is_powered) {
      // 1 means the sensor is powered
      snprintf(buff[3], 60, "powered.");
    } else {
      snprintf(buff[3], 60, "not powered.");
    }

    if (math_saturation) {
      // 1 means internal math saturation has occurred
      snprintf(buff[7], 60, "occurred.");
    } else {
      snprintf(buff[7], 60, "not occurred.");
    }

    if (integrity_test_passed) {
      // 1 means the checksum-based integrity check passed
      snprintf(buff[11], 60, "failed.");
    } else {
      snprintf(buff[11], 60, "passed.");
    }

    if (is_busy) {
      // 1 means the sensor is busy and the data for the 
      // last command is not yet available
      snprintf(buff[14], 60, "busy. The data");
      snprintf(buff[15], 60, "is not yet available.");
    } else {
      snprintf(buff[14], 60, "not busy. The data");
      snprintf(buff[15], 60, "is available.");
    }

    int i;
    int arr[5] = {3, 7, 11, 14, 15};
    // The indices of strings in the buffer that 
    // are constantly updated

    for (i = 0; i < 5; i++) {
      lcd.ClearStringLine((uint32_t) arr[i] + 1);
    }
    // Clear the lines that will be refreshed to avoid 
    // text retention

    for (i = 0; i < 19; i++) {
      lcd.DisplayStringAt(3, LINE(i + 1), (uint8_t *) buff[i], LEFT_MODE);
    }

    sleep_and_update_pressure();
  }

  lcd.Clear(LCD_COLOR_BLACK);
}

// Make the background layer visible and transparent, 
// reset all colors on the layer to black, and set the 
// text color to green
void setup_lcd_background() {
  lcd.SelectLayer(BACKGROUND);
  // Select the background layer
  lcd.Clear(LCD_COLOR_BLACK);
  // Reset all colors on the layer to black
  lcd.SetBackColor(LCD_COLOR_BLACK);
  // Set the background color to black
  lcd.SetTextColor(LCD_COLOR_GREEN);
  // Set the text color to green
  lcd.SetLayerVisible(BACKGROUND, ENABLE);
  // Make the background layer visible
  lcd.SetTransparency(BACKGROUND, 0x7Fu);
  // Set the background transparency to 0x7F
  // The transparency value ranges from 0x00 to 0xFF
}

// Reset all colors on the foreground layer to black
// and set the text color to light green
void setup_lcd_foreground() {
  lcd.SelectLayer(FOREGROUND);
  // Select the foreground layer
  lcd.Clear(LCD_COLOR_BLACK);
  // Reset all colors on the LCD to black
  lcd.SetBackColor(LCD_COLOR_BLACK);
  // Set the background color to black
  lcd.SetTextColor(LCD_COLOR_LIGHTGREEN);
  // Set the text color to light green
}

void pump_up_to_150() {
  read_pressure();
  // Update the pressure value
  char buffer[10][60];
  // A buffer for storing displayed texts
  lcd.Clear(LCD_COLOR_BLACK);
  // Clear the display to avoid text retention

  while (pressure < 150) {
    if (in_debug_mode) {
      // Call the debug mode function when the 
      // user has pressed the blue button
      debug_mode();
    }

    snprintf(buffer[0], 60, "Current pressure:");
    snprintf(buffer[1], 60, "%d mmHg", pressure);
    snprintf(buffer[2], 60, " ");
    // Leave an empty line in between
    snprintf(buffer[3], 60, "Keep pumping until");
    snprintf(buffer[4], 60, "pressure reaches");
    snprintf(buffer[5], 60, "150 mmHg");
    snprintf(buffer[6], 60, " ");
    snprintf(buffer[7], 60, "Press the blue");
    snprintf(buffer[8], 60, "button to enter");
    snprintf(buffer[9], 60, "Debug Mode");
    // Store the texts as a C-strings in the display buffer in 
    // order to display them on the LCD
    lcd.SelectLayer(FOREGROUND);
    // Use the foregound layer to display the text
    lcd.ClearStringLine(2);
    // Clear Line 2 before refreshing the pressure value 
    // to avoid text retention

    int i;
    for (i = 0; i < 10; i++) {
      lcd.DisplayStringAt(3, LINE(i + 1), (uint8_t *)buffer[i], LEFT_MODE);
      // Display each text in the buffer at the coordinate (0, LINE(i + 1)), 
      // using the left mode
    }

    sleep_and_update_pressure();
  }

  lcd.Clear(LCD_COLOR_BLACK);
  // Clear the LCD before the function returns
}

void open_valve() {
  read_pressure();
  // Update the pressure value
  char buffer[20][60];
  // A buffer for storing displayed texts

  uint16_t vals[90][10] = {0};
  // A 90-by-10 matrix for storing the pressure values read
  // while the cuff deflates
  // Initialize the first element as 0, and
  // all other elements will be automatically initialized as zeros
  int c_cnt = sizeof(vals[0]) / 2;
  // The number of columns in the matrix
  int r_cnt = sizeof(vals) / 2 / c_cnt;
  // The number of rows in the matrix
  int r = 0;
  // Stores the current row number
  int c = 0;
  // Stores the current column number
  int i = 0;
  // Initialize the index to be used in for loops

  snprintf(buffer[0], 60, "Current pressure:");
  snprintf(buffer[1], 60, "%d mmHg", pressure);
  snprintf(buffer[2], 60, " ");
  // Leave an empty line in between
  snprintf(buffer[3], 60, "Slightly open valve");
  snprintf(buffer[4], 60, "to make pressure drop");
  snprintf(buffer[5], 60, "at 4 mmHg/sec");
  snprintf(buffer[6], 60, " ");
  // Leave an empty line in between
  // Store the texts as a C-strings in the display buffer in 
  // order to display them on the LCD

  while (pressure > 30 && (!restarted_after_timeout)) {
    // Keep displaying the following text while the pressure is above 
    // 30 mmHg and the program hasn't restarted because of a timeout

    if (in_debug_mode) {
      // Call the debug mode function when the user 
      // has pressed the blue button
      debug_mode();
    }

    lcd.SelectLayer(FOREGROUND);
    // Use the foregound layer to display the text
    lcd.ClearStringLine(2);
    lcd.ClearStringLine(8);
    lcd.ClearStringLine(9);
    // Clear Lines 2, 8 and 9 before refreshing the texts to 
    // avoid text retention
    snprintf(buffer[1], 60, "%d mmHg", pressure);
    // Update the pressure value in the buffer

    for (i = 0; i < 9; i++) {
      if (r < 2 && (i == 7 || i == 8)) {
        // If the release rate hasn't been determined, 
        // do not display the 7th & 8th strings in the buffer, 
        // which are about the release rate
        // Move on to the next iteration
        continue;
      }

      lcd.DisplayStringAt(3, LINE(i + 1), (uint8_t *)buffer[i], LEFT_MODE);
      // Display each text in the buffer at the coordinate (0, LINE(i + 1)), 
      // using the left mode
    }

    sleep_and_update_pressure();
    vals[r][c] = pressure;
    // Store the current pressure in the array
    if (r > 0 && c == 0) {
      check_release_rate(r, vals[0], buffer[0]);
      // Check if the release is too fast or too slow and 
      // update the text in the buffer accordingly
      // Only call the function when c == 0, which happens
      // once every second
    }

    c++;
    // Increment the column index
    if (c >= 10) {
      r++;
      c = 0;
      // If the column pointer is at the end of the row, 
      // increment the row index and reset the column 
      // index to 0
    }

    if (r >= r_cnt) {
      // Means deflation took more than 90 seconds
      restarted_after_timeout = 1;
      // Set this to 1 so that timeout_restart will be 
      // called later
    }
  }

  for (i = 0; i < 900; i++) {
    vals_1d[i] = 0;
  }
  // Clear the integer array on the heap

  i = 0;
  for (r = 0; r < r_cnt; r++) {
    for (c = 0; c < c_cnt; c++) {
      vals_1d[i] = (int) vals[r][c];
      i++;
    }
  }
  // Copy everything in the 2D array into the 1D array

  lcd.Clear(LCD_COLOR_BLACK);
  // Clear the LCD before the function returns

  if (restarted_after_timeout) {
    // Call timeout_restart if deflation took 
    // more than 90 seconds
    timeout_restart();
  }
}

void calc_stats() {
  int t1 = 0;
  // Stores the index of the pressure read when the first 
  // heart beat was detected
  int t2 = 0;
  // Stores the index of the pressure read when the last
  // heart beat was detected. Since the time interval between 
  int mean_ap = 0;
  // Stores the mean arterial pressure (MAP)
  int cnt = 0;
  // Stores the total number of heart beats detected, which will be 
  // used to calculate the heart rate
  int i;

  for (i = 0; i < 900; i++) {
    osci[i] = 0;
  }
  // Clear the values in the array on the heap

  int last_inc = 0;
  // 1 if the last change in pressure reading was 
  // positive and 0 if negative
  // If the last change was 0, this variable stays
  // unchanged

  for (i = 1; i < 900; i++) {
    if (vals_1d[i] >= 150) {
      continue;
      // Skip the pressure values above 150 mmHg
    }

    osci[i] = (int) vals_1d[i] - vals_1d[i - 1];
    // The difference between two successive pressure values
    if (last_inc == 1 && osci[i] < 0) {
      // If the last change in reading was positive and 
      // the current change is negative, that indicates 
      // a heart beat
      if (cnt == 0) {
        // Means this is the first heart beat
        t1 = i - 1;
        // Write the index of the previous reading into t1
        systolic = vals_1d[i - 1];
        // The pressure value at the previous index is the 
        // systolic pressure
      }
      cnt++;
      // Increment the total number of heart beats detected
      t2 = i - 1;
      // Update the index of the reading when the most recent
      // heart beat was detected
    }

    if (osci[i] > 0) {
      last_inc = 1;
      // Change last_inc to 1 if the current change in 
      // pressure is positive
    } else if (osci[i] < 0) {
      last_inc = 0;
      // Change it to 0 if the current change is negative
    }
    // Note that last_inc stays unchanged if the current 
    // change is 0
  }

  int max_inc = 0;
  // Stores the maximum increase in pressure
  int curr_inc = 0;
  // Stores the cumulative increase in pressure since 
  // the last drop

  for (i = 0; i < 900; i++) {
    if (osci[i] < 0) {
      // If the current change in pressure is negative, 
      // the pressure wave is dropping, so curr_inc is 
      // the cumulative increase in pressure during the 
      // last spike
      if (curr_inc > max_inc) {
        // If the current increase is larger than the 
        // maximum, update the maximum
        max_inc = curr_inc;
        mean_ap = vals_1d[i - 1];
        // The mean arterial pressure is roughly equal to 
        // the pressure read at the maximum spike, so 
        // it should be updated with the last pressure 
        // reading whenever a greater spike is found
      }
      curr_inc = 0;
      // The current change in pressure is negative, 
      // so the cumulative increase should be reset to 0
    } else {
      curr_inc += osci[i];
      // If the current change in pressure is non-negative, 
      // add it to the cumulative increase
    }
  }

  diastolic = (mean_ap * 3 - systolic) / 2;
  // The formula for calculating the diastolic pressure when given 
  // the MAP and the systolic pressure
  heart_rate = cnt * 60 / ((t2 - t1) * 120 / 1000);
  // The heart rate in beats by minute equals the number of heart beats 
  // divided by the time interval in seconds and then multiplied by 60
  // The pressure is supposed to be read every 1/10 of a second, 
  // but every call to enter_operating_mode comes with a 10-ms delay
  // read_pressure makes at least 2 calls to enter_operating_mode, 
  // so the interval between 2 pressure readings is 120 ms, to be exact
}

void check_release_rate(int r, uint16_t * matrix, char * buffer) {
  // Compare the current pressure value to the previous one 
  // to see if the release rate is too high or too low

  // r is the row number in the matrix where the last pressure value 
  // was written

  if (r == 0) {
    // If it's only been less than a second, 
    // there's no comparison that can be made, so the function 
    // should return
    return;
  }

  int curr = *(matrix + r * 10);
  // The most recent pressure value read
  int prev = *(matrix + (r - 1) * 10);
  // The pressure value read a second ago

  if (prev - curr > 6) {
    // If the pressure value dropped by more than 
    // 6 mmHg in a second, the deflation is too fast
    snprintf(buffer + 7 * 60, 60, "Deflation is");
    snprintf(buffer + 8 * 60, 60, "TOO FAST.");
    // Update the text in the buffer accordingly
  } else if ((prev - curr > 0) && (prev - curr < 4)) {
    // If the pressure value dropped by less than
    // 4 mmHg in a second, the deflation is too slow.
    snprintf(buffer + 7 * 60, 60, "Deflation is");
    snprintf(buffer + 8 * 60, 60, "TOO SLOW.");
  } else {
    // Otherwise the deflation rate is OK
    // Note that if curr > prev because of a heart beat, 
    // the function also goes into this block
    snprintf(buffer + 7 * 60, 60, "Deflation is OK.");
    snprintf(buffer + 8 * 60, 60, "Maintain speed.");
  }
}

void timeout_restart() {
  // Restart the program when the pressure wasn't lowered to 30 mmHg 
  // within 90 seconds. The array in open_valve can only keep storing 
  // pressure values for 90 seconds, so the program will have to 
  // restart when the array runs out of space

  char buffer[10][60];
  // A buffer for storing texts to be displayed
  int countdown = 30;
  // Stores the number of seconds left before the 
  // program restarts

  snprintf(buffer[0], 60, "Sorry, the deflation");
  // Write the texts into the buffer
  snprintf(buffer[1], 60, "took you too long.");
  snprintf(buffer[2], 60, "Please restart from");
  snprintf(buffer[3], 60, "the beginning.");
  snprintf(buffer[4], 60, " ");
  // Leave an empty line in between
  snprintf(buffer[5], 60, "The program will");
  snprintf(buffer[6], 60, "restart in ");
  snprintf(buffer[7], 60, "%d seconds.", countdown);

  lcd.SelectLayer(FOREGROUND);
  // Use the foregound layer to display the text
  lcd.Clear(LCD_COLOR_BLACK);
  // Clear the display before displaying text to avoid text retention

  while (countdown) {
    lcd.ClearStringLine(8);
    // Clear Line 8 before refreshing the countdown value 
    // to avoid text retention
    int i;
    for (i = 0; i < 8; i++) {
      lcd.DisplayStringAt(3, LINE(i + 1), (uint8_t *)buffer[i], LEFT_MODE);
      // Display each text in the buffer at the coordinate (0, LINE(i + 1)), 
      // using the left mode
    }

    thread_sleep_for(1000);
    // Sleep for a second and then decrement the countdown
    countdown--;
    // Decrement the countdown value
    snprintf(buffer[7], 60, "%d seconds.", countdown);
    // Update the countdown value in the buffer
  }

  lcd.Clear(LCD_COLOR_BLACK);
  // Clear the LCD before the function returns
}

void show_stats() {
  // Display the heart rate, systolic value and diastolic value on the LCD

  char buffer[10][60];
  // A buffer for storing displayed texts

  int countdown = 30;
  // For tracking the number of seconds left to count

  snprintf(buffer[0], 60, "Heart rate: %d bpm", heart_rate);
  snprintf(buffer[1], 60, "Systolic: %d mmHg", systolic);
  snprintf(buffer[2], 60, "Diastolic: %d mmHg", diastolic);
  // heart_rate, systolic and diastolic are global variables, 
  // and their values have been updated by the calc_stats function
  snprintf(buffer[3], 60, " ");
  // Leave an empty line in between
  snprintf(buffer[4], 60, "Program will start ");
  snprintf(buffer[5], 60, "over in %d seconds", countdown);

  lcd.SelectLayer(FOREGROUND);
  // Use the foregound layer to display the text
  lcd.Clear(LCD_COLOR_BLACK);
  // Clear the display before displaying text to avoid text retention

  while (countdown) {
    int i;
    for (i = 0; i < 6; i++) {
      lcd.DisplayStringAt(3, LINE(i + 1), (uint8_t *)buffer[i], LEFT_MODE);
      // Display each text in the buffer at the coordinate (0, LINE(i + 1)), 
      // using the left mode
    }

    thread_sleep_for(1000);
    // Sleep for a second and then decrement the countdown
    countdown--;
    // Decrement the countdown value
    snprintf(buffer[5], 60, "over in %d seconds", countdown);
    // Update the countdown value in the buffer
    lcd.ClearStringLine(6);
    // Clear Line 6 before refreshing the countdown value
    // to avoid text retention
  }

  lcd.Clear(LCD_COLOR_BLACK);
  // Clear the LCD before the function returns
}

int main() {
  setup_lcd_background();
  setup_lcd_foreground();
  // Prepare the LCD for displaying texts

  button_int.rise(&button_isr);
  // If a button interrupt occurs, call the button ISR

  while(1) {
    restarted_after_timeout = 0;
    // Reset this to 0 at the beginning of every iteration
    pump_up_to_150();
    // Ask the user to keep pumping until the 
    // pressure reaches 150 mmHg
    open_valve();
    // Ask the user to open the valve

    if (!restarted_after_timeout) {
      // Only call these functions if the program didn't restart 
      // because of a timeout
      calc_stats();
      // Calculate the heart rate, the systolic pressure and 
      // the diastolic pressure
      show_stats();
      // Display the stats on the LCD
    }
  }
}
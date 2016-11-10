/*
 * STMicroelectronics sensors spi library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef ST_SENSORS_SPI_H
#define ST_SENSORS_SPI_H

#include <linux/spi/spi.h>
#include <linux/iio/common/st_sensors.h>

int st_sensors_spi_probe(struct spi_device *spi,
			 struct iio_dev **ret_indio_dev);

#endif /* ST_SENSORS_SPI_H */

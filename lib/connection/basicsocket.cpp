/*
 * $Header: /cvs/tuxbox/apps/misc/libs/libconnection/basicsocket.cpp,v 1.2 2003/02/24 21:14:15 thegoodguy Exp $
 *
 * Basic Socket Class - The Tuxbox Project
 *
 * (C) 2003 by thegoodguy <thegoodguy@berlios.de>
 *
 * License: GPL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "basicsocket.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>

bool send_data(int fd, const void * data, const size_t size, const timeval timeout)
{
	char *buffer = (char *)data;
	size_t left = size;

	while (left > 0)
	{
		int len = ::send(fd, buffer, left, MSG_DONTWAIT | MSG_NOSIGNAL);

		if (len < 0)
		{
			perror("[basicsocket] send_data");

			if (errno != EINTR && errno != EAGAIN)
				return false;

			struct pollfd pfd;
			pfd.fd = fd;
			pfd.events = POLLOUT;
			pfd.revents = 0;

			int rc = poll(&pfd, 1, timeout.tv_sec * 1000 + timeout.tv_usec / 1000);

			if (rc == 0)
			{
				printf("[basicsocket] send timed out.\n");
				return false;
			}
			if (rc < 0)
			{
				perror("[basicsocket] send_data poll");
				return false;
			}
			if (!(pfd.revents & POLLOUT))
			{
				perror("[basicsocket] send_data POLLOUT");
				return false;
			}
		}
		else
		{
			buffer += len;
			left -= len;
		}
	}
	return true;
}


bool receive_data(int fd, void * data, const size_t size, const timeval timeout)
{
	char *buffer = (char *)data;
	size_t left = size;

	while (left > 0)
	{
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		int to = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;

		int rc = poll(&pfd, 1, to);

		if (rc == 0)
		{
			printf("[basicsocket] recv timed out.\n");
			return false;
		}
		if (rc < 0)
		{
			perror("[basicsocket] recv_data poll");
			return false;
		}
		if (!(pfd.revents & POLLIN))
		{
			perror("[basicsocket] recv_data POLLIN");
			return false;
		}
		int len = ::recv(fd, data, left, MSG_DONTWAIT | MSG_NOSIGNAL);

		if (len > 0) {
			left -= len;
			buffer += len;
		} else if (len < 0)
		{
			perror("[basicsocket] receive_data");
			if (errno != EINTR && errno != EAGAIN)
				return false;
		}
		else // len == 0
		{
			/*
			 * silently return false
			 *
			 * printf("[basicsocket] no more data\n");
			 */
			return false;
		}
	}
	return true;
}

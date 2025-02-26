/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file packet.h Basic functions to create, fill and read packets.
 */

#ifndef NETWORK_CORE_PACKET_H
#define NETWORK_CORE_PACKET_H

#include "os_abstraction.h"
#include "config.h"
#include "core.h"
#include "../../string_type.h"
#include "../../core/serialisation.hpp"
#include <string>
#include <functional>
#include <limits>
#include <vector>

typedef uint16_t PacketSize; ///< Size of the whole packet.
typedef uint8_t  PacketType; ///< Identifier for the packet

/**
 * Internal entity of a packet. As everything is sent as a packet,
 * all network communication will need to call the functions that
 * populate the packet.
 * Every packet can be at most a limited number bytes set in the
 * constructor. Overflowing this limit will give an assertion when
 * sending (i.e. writing) the packet. Reading past the size of the
 * packet when receiving will return all 0 values and "" in case of
 * the string.
 *
 * --- Points of attention ---
 *  - all > 1 byte integral values are written in little endian,
 *    unless specified otherwise.
 *      Thus, 0x01234567 would be sent as {0x67, 0x45, 0x23, 0x01}.
 *  - all sent strings are of variable length and terminated by a '\0'.
 *      Thus, the length of the strings is not sent.
 *  - years that are leap years in the 'days since X' to 'date' calculations:
 *     (year % 4 == 0) and ((year % 100 != 0) or (year % 400 == 0))
 */
struct Packet : public BufferSerialisationHelper<Packet>, public BufferDeserialisationHelper<Packet> {
private:
	/** The current read/write position in the packet */
	PacketSize pos;
	/** The buffer of this packet. */
	std::vector<uint8_t> buffer;
	/** The limit for the packet size. */
	size_t limit;

	/** Socket we're associated with. */
	NetworkSocketHandler *cs;

public:
	Packet(NetworkSocketHandler *cs, size_t limit, size_t initial_read_size = sizeof(PacketSize));
	Packet(PacketType type, size_t limit = COMPAT_MTU);

	void ResetState(PacketType type);

	/* Sending/writing of packets */
	void PrepareToSend();

	std::vector<uint8_t> &GetSerialisationBuffer() { return this->buffer; }
	size_t GetSerialisationLimit() const { return this->limit; }

	const uint8_t *GetDeserialisationBuffer() const { return this->buffer.data(); }
	size_t GetDeserialisationBufferSize() const { return this->buffer.size(); }
	PacketSize &GetDeserialisationPosition() { return this->pos; }
	bool CanDeserialiseBytes(size_t bytes_to_read, bool raise_error) { return this->CanReadFromPacket(bytes_to_read, raise_error); }

	bool CanWriteToPacket(size_t bytes_to_write);

	void WriteAtOffset_uint16(size_t offset, uint16_t);

	/* Reading/receiving of packets */
	size_t ReadRawPacketSize() const;
	bool HasPacketSizeData() const;
	bool ParsePacketSize();
	size_t Size() const;
	void PrepareToRead();
	PacketType GetPacketType() const;

	bool CanReadFromPacket(size_t bytes_to_read, bool close_connection = false);

	size_t RemainingBytesToTransfer() const;

	const uint8_t *GetBufferData() const { return this->buffer.data(); }
	PacketSize GetRawPos() const { return this->pos; }
	void ReserveBuffer(size_t size) { this->buffer.reserve(size); }

	/**
	 * Transfer data from the packet to the given function. It starts reading at the
	 * position the last transfer stopped.
	 * See Packet::TransferIn for more information about transferring data to functions.
	 * @param transfer_function The function to pass the buffer as second parameter and the
	 *                          amount to write as third parameter. It returns the amount that
	 *                          was written or -1 upon errors.
	 * @param limit             The maximum amount of bytes to transfer.
	 * @param destination       The first parameter of the transfer function.
	 * @param args              The fourth and further parameters to the transfer function, if any.
	 * @return The return value of the transfer_function.
	 */
	template <
		typename A = size_t, ///< The type for the amount to be passed, so it can be cast to the right type.
		typename F,          ///< The type of the function.
		typename D,          ///< The type of the destination.
		typename ... Args>   ///< The types of the remaining arguments to the function.
	ssize_t TransferOutWithLimit(F transfer_function, size_t limit, D destination, Args&& ... args)
	{
		size_t amount = std::min(this->RemainingBytesToTransfer(), limit);
		if (amount == 0) return 0;

		assert(this->pos < this->buffer.size());
		assert(this->pos + amount <= this->buffer.size());
		/* Making buffer a char means casting a lot in the Recv/Send functions. */
		const char *output_buffer = reinterpret_cast<const char*>(this->buffer.data() + this->pos);
		ssize_t bytes = transfer_function(destination, output_buffer, static_cast<A>(amount), std::forward<Args>(args)...);
		if (bytes > 0) this->pos += bytes;
		return bytes;
	}

	/**
	 * Transfer data from the packet to the given function. It starts reading at the
	 * position the last transfer stopped.
	 * See Packet::TransferIn for more information about transferring data to functions.
	 * @param transfer_function The function to pass the buffer as second parameter and the
	 *                          amount to write as third parameter. It returns the amount that
	 *                          was written or -1 upon errors.
	 * @param destination       The first parameter of the transfer function.
	 * @param args              The fourth and further parameters to the transfer function, if any.
	 * @tparam A    The type for the amount to be passed, so it can be cast to the right type.
	 * @tparam F    The type of the transfer_function.
	 * @tparam D    The type of the destination.
	 * @tparam Args The types of the remaining arguments to the function.
	 * @return The return value of the transfer_function.
	 */
	template <typename A = size_t, typename F, typename D, typename ... Args>
	ssize_t TransferOut(F transfer_function, D destination, Args&& ... args)
	{
		return TransferOutWithLimit<A>(transfer_function, std::numeric_limits<size_t>::max(), destination, std::forward<Args>(args)...);
	}

	/**
	 * Transfer data from the given function into the packet. It starts writing at the
	 * position the last transfer stopped.
	 *
	 * Examples of functions that can be used to transfer data into a packet are TCP's
	 * recv and UDP's recvfrom functions. They will directly write their data into the
	 * packet without an intermediate buffer.
	 * Examples of functions that can be used to transfer data from a packet are TCP's
	 * send and UDP's sendto functions. They will directly read the data from the packet's
	 * buffer without an intermediate buffer.
	 * These are functions are special in a sense as even though the packet can send or
	 * receive an amount of data, those functions can say they only processed a smaller
	 * amount, so special handling is required to keep the position pointers correct.
	 * Most of these transfer functions are in the form function(source, buffer, amount, ...),
	 * so the template of this function will assume that as the base parameter order.
	 *
	 * This will attempt to write all the remaining bytes into the packet. It updates the
	 * position based on how many bytes were actually written by the called transfer_function.
	 * @param transfer_function The function to pass the buffer as second parameter and the
	 *                          amount to read as third parameter. It returns the amount that
	 *                          was read or -1 upon errors.
	 * @param source            The first parameter of the transfer function.
	 * @param args              The fourth and further parameters to the transfer function, if any.
	 * @tparam A    The type for the amount to be passed, so it can be cast to the right type.
	 * @tparam F    The type of the transfer_function.
	 * @tparam S    The type of the source.
	 * @tparam Args The types of the remaining arguments to the function.
	 * @return The return value of the transfer_function.
	 */
	template <typename A = size_t, typename F, typename S, typename ... Args>
	ssize_t TransferIn(F transfer_function, S source, Args&& ... args)
	{
		size_t amount = this->RemainingBytesToTransfer();
		if (amount == 0) return 0;

		assert(this->pos < this->buffer.size());
		assert(this->pos + amount <= this->buffer.size());
		/* Making buffer a char means casting a lot in the Recv/Send functions. */
		char *input_buffer = reinterpret_cast<char*>(this->buffer.data() + this->pos);
		ssize_t bytes = transfer_function(source, input_buffer, static_cast<A>(amount), std::forward<Args>(args)...);
		if (bytes > 0) this->pos += bytes;
		return bytes;
	}

	NetworkSocketHandler *GetParentSocket() { return this->cs; }
};

struct SubPacketDeserialiser : public BufferDeserialisationHelper<SubPacketDeserialiser> {
	NetworkSocketHandler *cs;
	const uint8_t *data;
	size_t size;
	PacketSize pos;

	SubPacketDeserialiser(Packet &p, const uint8_t *data, size_t size, PacketSize pos = 0) : cs(p.GetParentSocket()), data(data), size(size), pos(pos) {}
	SubPacketDeserialiser(Packet &p, const std::vector<uint8_t> &buffer, PacketSize pos = 0) : cs(p.GetParentSocket()), data(buffer.data()), size(buffer.size()), pos(pos) {}

	const uint8_t *GetDeserialisationBuffer() const { return this->data; }
	size_t GetDeserialisationBufferSize() const { return this->size; }
	PacketSize &GetDeserialisationPosition() { return this->pos; }
	bool CanDeserialiseBytes(size_t bytes_to_read, bool raise_error);
};

#endif /* NETWORK_CORE_PACKET_H */

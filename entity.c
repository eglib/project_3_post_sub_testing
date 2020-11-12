/******************************************************************************/
/*                                                                            */
/* ENTITY IMPLEMENTATIONS                                                     */
/*                                                                            */
/******************************************************************************/

// Student names:Bilge Batsukh, Kendall Livesay
// Student computing IDs: eb5yf, krl7ee
//
//
// This file contains the actual code for the functions that will implement the
// reliable transport protocols enabling entity "A" to reliably send information
// to entity "B".
//
// This is where you should write your code, and you should submit a modified
// version of this file.
//
// Notes:
// - One way network delay averages five time units (longer if there are other
//   messages in the channel for GBN), but can be larger.
// - Packets can be corrupted (either the header or the data portion) or lost,
//   according to user-defined probabilities entered as command line arguments.
// - Packets will be delivered in the order in which they were sent (although
//   some can be lost).
// - You may have global state in this file, BUT THAT GLOBAL STATE MUST NOT BE
//   SHARED BETWEEN THE TWO ENTITIES' FUNCTIONS. "A" and "B" are simulating two
//   entities connected by a network, and as such they cannot access each
//   other's variables and global state. Entity "A" can access its own state,
//   and entity "B" can access its own state, but anything shared between the
//   two must be passed in a `pkt` across the simulated network. Violating this
//   requirement will result in a very low score for this project (or a 0).
//
// To run this project you should be able to compile it with something like:
//
//     $ gcc entity.c simulator.c -o myproject
//
// and then run it like:
//
//     $ ./myproject 0.0 0.0 10 500 3 8 test1.txt
//
// Of course, that will cause the channel to be perfect, so you should test
// with a less ideal channel, and you should vary the random seed. However, for
// testing it can be helpful to keep the seed constant.
//
// The simulator will write the received data on entity "B" to a file called
// `output.dat`.

#include <stdio.h>
#include <string.h>
#include "simulator.h"
#include <limits.h>
#include <stdlib.h>
#define ACK 0
// #define NACK 1 //used in abp
#define TRUE 1
#define FALSE 0
#define TIMEOUT 25.0
#define MAX_BUFFER_SIZE 7000
/*this badboy method down here may or may not come in handy later when cleaning stuff up - for now just don't touchie touchie!*/
// int buildChecksum(struct msg message, struct pkt packet)
// {
//     //converts a packet (and associated message) into a checksum - could be used by sender
//     int checksum = 0;
//     for (int i = 0; i < message.length; i++)
//     {
//         checksum += message.data[i];
//     }
// }

/**** A ENTITY ****/
// remember - lowest_unacked_a is not incremented unless a good ACK is received
int a_sequence; //global (A) for the current sequence number that A is holding
int lowest_unacked_a; //global (A) for the lowest unACKed packet number that we have sent (should be seqnum of first)
//int message_in_transit_a;       //global (A) for whether or not a packet is in transit (alternating bit only)
//struct msg last_sent_message_a; //global (A) to retain last sent message in case of corruption or loss.(alternating bit only)
struct pkt oldest_sent_packet_a; //global (A) to retain last sent packet
struct Queue *window;
struct Queue *buffer;
// in this scenario, the packet header has k-bits for sequence number - k is 8 here!
//TODO: requirements for a window - hold sent up to N sent packets - remove them FIFO - this is describing a queue

//queue code taken from https://www.geeksforgeeks.org/queue-set-1introduction-and-array-implementation/
//pls no bully i just don't want to write a queue I know it's overkill
// A structure to represent a queue
struct Queue
{
    int front, rear, size;
    unsigned capacity;
    struct pkt *array;
};

// function to create a queue
// of given capacity.
// It initializes size of queue as 0
struct Queue *createQueue(unsigned capacity)
{
    struct Queue *queue = (struct Queue *)malloc(
        sizeof(struct Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;

    // This is important, see the enqueue
    queue->rear = capacity - 1;
    queue->array = (struct pkt *)malloc(
        queue->capacity * sizeof(struct pkt));
    return queue;
}

// Queue is full when size becomes
// equal to the capacity
int isFull(struct Queue *queue)
{
    return (queue->size == queue->capacity);
}

// Queue is empty when size is 0
int isEmpty(struct Queue *queue)
{
    return (queue->size == 0);
}

// Function to add an item to the queue.
// It changes rear and size
void enqueue(struct Queue *queue, struct pkt packet)
{
    if (isFull(queue))
        return;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->array[queue->rear] = packet;
    queue->size = queue->size + 1;
}

// Function to remove an item from queue.
// It changes front and size
struct pkt dequeue(struct Queue *queue)
{
    struct pkt packet;
    if (isEmpty(queue))
        return packet;
    packet = queue->array[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size = queue->size - 1;
    return packet;
}

// Function to get front of queue
struct pkt front(struct Queue *queue)
{
    struct pkt packet;
    if (isEmpty(queue))
        return packet;
    return queue->array[queue->front];
}

// // Function to get rear of queue
// struct pkt rear(struct Queue *queue)
// {
//     struct pkt packet;
//     if (isEmpty(queue))
//         return packet;
//     return queue->array[queue->rear];
// }

void A_init(int window_size)
{
    //do any initialization here - this function is only called once, before any other functions are called
    a_sequence = -1;        // prepare the sequence
    lowest_unacked_a = -1;  // first packet (0) will be considered not acked to start (obviously)
    window = createQueue(window_size);     //N size transmit window
    buffer = createQueue(MAX_BUFFER_SIZE); //buffer for when the window is filled
}

void A_output(struct msg message) //use only when creating and sending a packet for the first time - retransmits should utilize tolayer3_A(struct pkt packet)
{
    struct pkt packet;
    //in any case, we should form a packet ready to be sent or queued.
    memcpy(packet.payload, message.data, message.length);
    packet.length = message.length;
    if (packet.length < 32)
        for (int k = packet.length; k < 32; k++) //guarantee all 32 indices in packet.payload are initialized - only up to message.length (and therefore packet.length) are they relevant
            packet.payload[k] = 0;
    //need to also assign other packet fields - int seqnum, int acknum, int checksum
    packet.seqnum = a_sequence+1; //will be the sequence number of the ACK message from B as well, once received without corruption
    a_sequence++; //keeps track of packet sequence
    packet.acknum = packet.seqnum;
    //time to build a checksum
    packet.checksum = 0;
    for (int i = 0; i < 32; i++)
    {                                         //we can use 32 because we can guarantee all 32 indices in packet.payload are initialized
        packet.checksum += packet.payload[i]; //use the carried data
    }
    packet.checksum += packet.acknum + packet.seqnum; //and the ack/seq nums

    // ok, packet formation is done now - what to do?
    if (isEmpty(buffer))
    {
        //case 1 of 4 - the buffer is empty and the window is not full
        if (!isFull(window))
        {
            enqueue(window, packet); // add the packet to the window and send it
            printf("A sent packet before previous was ACKed\n");
            tolayer3_A(packet);      //bombs away!
            // the below code (and any replications of it) WILL NOT WORK for GBN protocol - be aware
            // if (packet.seqnum <= a_sequence) //if this packet is the "lowest unacked value" we need to update lowest unacked value
            // {
            //     stoptimer_A();
            //     starttimer_A(TIMEOUT);      //if this is really the lowest seqnum in our window, then we should start a new timer
            //     a_sequence = packet.seqnum+1; //update the lowest unacked value to reflect new lowest unack
            // }
        }
        //case 2 of 4 - the buffer is empty and the window is full (only happens once in a blue moon, hopefully)
        else if (isFull(window))
        {
            enqueue(buffer, packet); //simply buffer it and deal later
        }
        //side-case - the buffer is full
        else
        {
            free(buffer);
            free(window);
            exit(0); //we are allowed to give up!
        }

    } //this concludes scenarios 1 and 2, where the buffer was empty
    else
    {
        //case 3 of 4 - the buffer has something in it, the window is not full
        //
        if (!isFull(window))
        { //condition: there is space in the window for another packet to be prepped/sent
            //correct action: put the current message in the buffer, bump something from the buffer to the window! (make sure expected ack is still the LOWEST value!)
            enqueue(buffer, packet); //put the current packet in the buffer and forget about it.
            struct pkt packet2 = dequeue(buffer);
            printf("packet transitioned from buffer to window\n");
            enqueue(window, packet2);
            printf("A sent packet before previous was ACKed \n");
            tolayer3_A(packet2);
            // if (packet2.seqnum <= a_sequence) //this should never happen. but just in case, it's a safeguard for whenever we sent a packet.
            // {
            //     stoptimer_A();
            //     starttimer_A(TIMEOUT);       //if this is really the lowest seqnum in our window, then we should start a new timer
            //     a_sequence = packet2.seqnum; //update the recorded lowest unacked value to reflect new lowest unacked value
            // }                                //the rest of the lowest unacked value changing should be done by the input function for A
        }
        //case 4 of 4 - the buffer has something in it, and the window is full
        else if (isFull(window) && !isFull(buffer))
        {
            //condition: the window has no available slots, buffer has something in it, but is not full
            //correct action:  add packet to buffer, leave it as is.
            enqueue(buffer, packet);
        }
        //side case - buffer is full
        else if (isFull(buffer))
        {
            free(buffer);
            free(window);
            exit(0); //it's OK to give up
        }
    } //concludes all first-time packet-send cases - retransmits handled by timer and A_input
}
/*
void A_output(struct msg message) //precondition to transmit
{
    struct pkt packet;

    if (isEmpty(buffer))
    {
        //condition: the buffer has nothing in it
        //can't assign array by value with packet.payload = message.data;
        memcpy(packet.payload, message.data, message.length);
        packet.length = message.length;
        if (packet.length < 32)
            for (int k = packet.length; k < 32; k++) //guarantee all 32 indices in packet.payload are initialized - only up to message.length (and therefore packet.length) are they relevant
                packet.payload[k] = 0;
        //need to also assign other packet fields - int seqnum, int acknum, int checksum
        packet.seqnum = a_sequence; //will be the sequence number of the ACK message from B as well, once received without corruption
        packet.acknum = packet.seqnum;
        //time to build a checksum
        packet.checksum = 0;
        for (int i = 0; i < 32; i++)
        {                                         //we can use 32 because we can guarantee all 32 indices in packet.payload are initialized
            packet.checksum += packet.payload[i]; //use the carried data
        }
        packet.checksum += packet.acknum + packet.seqnum; //and the ack/seq nums
        if (!isFull(window))
        {
            enqueue(window, packet); // add the packet to the queue
            tolayer3_A(packet);      //bombs away!
            if (packet.seqnum <= a_sequence)
            {
                starttimer_A(TIMEOUT);      //if this is really the lowest seqnum in our window, then we should start a new timer
                a_sequence = packet.seqnum; //update the lowest unacked value to reflect new lowest unack
            }
        }
        else if (!isFull(buffer))
        {
            memcpy(packet.payload, message.data, message.length);
            packet.length = message.length;
            if (packet.length < 32)
                for (int x = packet.length; x < 32; x++) //guarantee all 32 indices in packet.payload are initialized - only up to message.length (and therefore packet.length) are they relevant
                    packet.payload[x] = 0;
            //need to also assign other packet fields - int seqnum, int acknum, int checksum
            packet.seqnum = a_sequence; //will be the sequence number of the ACK message from B as well, once received without corruption
            packet.acknum = packet.seqnum;
            //time to build a checksum
            packet.checksum = 0;
            for (int y = 0; y < 32; y++)
            {                                         //we can use 32 because we can guarantee all 32 indices in packet.payload are initialized
                packet.checksum += packet.payload[y]; //use the carried data
            }
            packet.checksum += packet.acknum + packet.seqnum; //and the ack/seq nums
            enqueue(buffer, packet);                          //if the window is full, send this packet to the buffer queue
        }
        else
            exit(0); //if the buffer is full too, just give up!
    }

    else
    { //condition: there exists something in the buffer
        if (!isFull(window))
        { //condition: there is space in the window for another packet to be prepped/sent
            //correct action: put it in the window ! (make sure ack is proper!)
            packet = dequeue(buffer);
            enqueue(window, packet);
            tolayer3_A(packet);
            if (packet.seqnum <= a_sequence) //this should never happen. but just in case.
            {
                starttimer_A(TIMEOUT);      //if this is really the lowest seqnum in our window, then we should start a new timer
                a_sequence = packet.seqnum; //update the lowest unacked value to reflect new lowest unack
            }
        }
        else if (isFull(window) && !isFull(buffer))
        {
            //condition: the window has no available slots, buffer has ANYTHING in it, but is not full
            //correct action: create packet with message, add to buffer
            memcpy(packet.payload, message.data, message.length);
            packet.length = message.length;
            if (packet.length < 32)
                for (int t = packet.length; t < 32; t++) //guarantee all 32 indices in packet.payload are initialized - only up to message.length (and therefore packet.length) are they relevant
                    packet.payload[t] = 0;
            //need to also assign other packet fields - int seqnum, int acknum, int checksum
            packet.seqnum = a_sequence; //will be the sequence number of the ACK message from B as well, once received without corruption
            packet.acknum = packet.seqnum;
            //time to build a checksum
            packet.checksum = 0;
            for (int u = 0; u < 32; u++)
            {                                         //we can use 32 because we can guarantee all 32 indices in packet.payload are initialized
                packet.checksum += packet.payload[u]; //use the carried data
            }
            packet.checksum += packet.acknum + packet.seqnum; //and the ack/seq nums
            enqueue(buffer, packet);                          //if the window is full, send this packet to the buffer queue
        }
        else if (isFull(buffer))
            exit(0);
    }
}*/

void A_input(struct pkt packet)
{
    // looking for an ACK packet with a matching sequence number
    int checksum = 0;
    //time to check a checksum of ACKet
    if (packet.length > 32 || packet.length < 0) //guaranteed corruption
        checksum = !packet.checksum;             //this will always fail the checksum == packet.checksum test
    else
    {
        for (int i = 0; i < 32; i++) //this is legal because we are still guaranteeing every index is initialized
        {
            checksum += packet.payload[i];
        }
        checksum += packet.acknum + packet.seqnum;
    }

    if (checksum == packet.checksum) //condition: the ACK was theoretically not corrupted
    {
        if (packet.acknum >= lowest_unacked_a)
        {
            //condition: B has received the packets up to and including packet.acknum
            //correct response: stop timer, prep to send next packet (by incrementing the sequence number to confirm we're ready to move on)
            stoptimer_A();
            lowest_unacked_a = packet.acknum;
            while (front(window).seqnum <= lowest_unacked_a && (!isEmpty(window))) //if there are packets that have already been acked, get rid of them
            {
                dequeue(window); //the aforementioned getting rid of them
                struct pkt bufferPkt;
                if (!isEmpty(buffer))
                { //if there's anything that needs to be sent that hasn't yet, put it in the window and send
                    bufferPkt = dequeue(buffer);
                    enqueue(window, bufferPkt);
                    tolayer3_A(bufferPkt);
                }
            }
            starttimer_A(TIMEOUT); //now if we start the timer again, it is "on top of" the oldest nonACKed packet right

            //if there is nothing in the window to get rid of, and there is no data waiting in the buffer... do nothing and wait for A_output to be called again?
            // else
            // {
            //     //condition: ACK is for the out-of-order packet somehow or has been corrupted(but still passes checksum - lucky bit change)
            //     //correct response: retransmit last packet, to be safe. at worst, the receiver has to deal with a duplicate packet
            //     stoptimer_A();
            //     A_output(last_sent_message_a);
            // }
        }

        else
        {
            //condition: ACK is for a previously ACKed packet. Either the packet at a_sequence was dropped or the ACK got corrupted
            //correct response: resend all unACKed packets in the window (which is all packets in the window)
            for(int winDex = 0; winDex < window->size ; winDex++) //if it's in the window, it should be resent
                {
                    struct pkt pkt2send;
                    pkt2send = window->array[window->front + winDex]; //get a packet from the window
                    tolayer3_A(pkt2send); //bombs away
                }
            
            // for (int k = packet.length; k < 32; k++) //guarantee all 32 indices in packet.payload are initialized - only up to message.length (and therefore packet.length) are they relevant
            //     packet.payload[k] = 0;
        }

        /**
        no longer using NACK for gbn

        else if (packet.acknum == NACK)
        {
            //checksum did not match on B side
            //condition: data was corrupted
            //correct response: retransmit last packet
            stoptimer_A();
            A_output(last_sent_message_a);
        }
        */
    }
    else
    {
        // do nothing and wait for timeout

        //condition: ACK/NACK was corrupted
        //if it was an ACK, the receiver does not need a retransmit, but should be able to handle a repeat packet
        //if it was a NACK, they do!
        //solution? send it anyways and believe the receiver won't pass up duplicate data
        //correct response: bombs away!
        /**
        stoptimer_A();
        A_output(last_sent_message_a);
        */
    }
}

void A_timerinterrupt()
{
    printf("Timer Interrupt A\n");
    //TODO: resend all unacked packets w/o shifting the window
    stoptimer_A();
    
    for (int i = 0; i < window->size; i++)
    {
        printf("Entity A retransmitted a packet\n");
        tolayer3_A(window->array[window->front + i]); //every packet in the window should be unACKed
    }
    if(!isEmpty(buffer) && !isEmpty(window))
        starttimer_A(TIMEOUT);
    /*
    //condition: our message seems to not have been received at all, or we never got the ACK/NACK
    //correct response: retransmit
    message_in_transit_a = FALSE; //assume our packet fell out in the air
    A_output(last_sent_message_a);*/
}

/**** B ENTITY ****/
/*
struct pkt last_sent_packet_b;     //global (b) to retain last sent packet in case of corruption or loss.(alternating bit only)
struct msg last_sent_message_b;    //global (b) to retain last sent message in case of retransmit need
int last_received_packet_number_b; //global (b) to retain last received sequence number
*/

int rcv_base; //global (B) to record the highest sent ACK value still in sequence order
void B_init(int window_size)
{
    rcv_base = -1; //prepare to send ack = 0
}

void B_input(struct pkt packet)
{
    int checksum = 0;
    if (packet.length > 32 || packet.length < 0)
        checksum = !packet.checksum;
    else
    {
        for (int i = 0; i < 32; i++) //entity A guarantees that packet.payload is fully initialized, even if corrupted
            checksum += packet.payload[i];
        checksum += packet.acknum + packet.seqnum;
    }
    if (checksum != packet.checksum)
    { //condition: received packet is corrupted
        //correct response: send previous acceptable ACK
        struct pkt ACKet;
        ACKet.acknum = rcv_base;
        ACKet.length = 0;
        for (int j = ACKet.length; j < 32; j++)
            ACKet.payload[j] = 0; //guarantee that ACKet.payload is initialized at every index
        ACKet.seqnum = packet.seqnum;
        ACKet.checksum += ACKet.seqnum + ACKet.acknum;
        tolayer3_B(ACKet); //send the ACK to entity A!
    }
    /**
        from abp
        //condition: packet is corrupted
        //correct response: send NACK to A
        struct pkt NACKet;
        NACKet.acknum = NACK;
        NACKet.length = 0;
        for (int j = NACKet.length; j < 32; j++)
            NACKet.payload[j] = 0; //guarantee that NACKet.payload is initialized at every index
        NACKet.seqnum = packet.seqnum;
        NACKet.checksum += NACKet.seqnum + NACKet.acknum;
        tolayer3_B(NACKet); // we don't expect anything back except the NACKed packet - which can be handled
        */
    else
    {
        //condition: packet is supposedly not corrupted
        //correct response: check sequence number against the expected sequence number (the last seqnum received + 1)
        if (packet.seqnum == rcv_base + 1)
        {
            //condition: sequence number matches the expectation, packet theoretically not corrupted and in right order
            //correct response: send ACK, pass message to application, set new rcv_base and rcv_base_decrement
            struct pkt ACKet;
            ACKet.acknum = packet.seqnum;
            ACKet.length = 0;
            for (int k = ACKet.length; k < 32; k++)
                ACKet.payload[k] = 0; //guarantee that ACKet.payload is initialized at every index
            ACKet.seqnum = packet.seqnum;
            ACKet.checksum += ACKet.seqnum + ACKet.acknum;
            tolayer3_B(ACKet);        //send the ACK to entity A!
            rcv_base = packet.seqnum; //guarantees we shouldn't pass up a repeat packet, since each packet can only come through here once?
            //this is where things get dicey - if we receive the same uncorrupted packet twice because of a corrupted ACK, we still pass it up?
            struct msg recv_msg;
            recv_msg.length = packet.length; //we should be able to trust packet.length at this point, hopefully
            memcpy(recv_msg.data, packet.payload, recv_msg.length);
            tolayer5_B(recv_msg);
        }
        else
        {
            //condition: the sequence number does NOT match the expected sequence number, what to do?
            //if entity A has given us an incorrect sequence number higher than rcv_base, drop it!
            //if entity A has given us an incorrect sequence number lower than rcv_base, it's probably corrupted or a retransmit (which should already be acked either itself or by a higher ack).
            // a) it has retransmitted a packet that's already been ACKed
            //  in which case, we should ACK it again because the ACK message could have been corrupted and it might be the last message
            // b) it timed out, but we already sent an ACK their way?
            //  in which case, we should do same as (a), since the result is the same whether the ACK was corrupted or lost -> retransmit and wait
            //so, ACKs away (for the wrong sequence number) - we can't increase rcv_base here though!
            struct pkt ACKet;
            ACKet.acknum = rcv_base;
            ACKet.length = 0;
            for (int x = ACKet.length; x < 32; x++)
                ACKet.payload[x] = 0; //guarantee that ACKet.payload is initialized at every index
            ACKet.seqnum = packet.seqnum;
            ACKet.checksum += ACKet.seqnum + ACKet.acknum;
            tolayer3_B(ACKet); //send the ACK to entity A!
        }
    }
}
//uh why on god's green earth do i need this (2:13 AM 10/30/2020 - bilge batsukh)
void B_timerinterrupt() {} // in fact, we have determined it is a bait (4:49 AM 10/30/2020 - bilge batsukh)

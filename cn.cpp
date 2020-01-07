
#include <stdio.h>
#include <string.h>
#include <omnetpp.h>

using namespace omnetpp;

#include "tictoc_m.h"

class Tic : public cSimpleModule
{
  private:
    simtime_t timeout;  // timeout for control
    int seq, count, flag, ackcount, size, seqmax;  // message sequence number(seq) and expected ack number(count)

    Tictoc *msg1;
    simtime_t nxtmsg; //timeout (data rate)
    Tictoc *nxtmsgEvent; //holds pointer to the timeout self-message of the next Message (data rate)

    Tictoc* timeoutmsgEvent; //holds pointer to the timeout self-message of the Message

    cQueue msgqueue; // queue for keeping a copy of msg until ack arrives


  protected:
    virtual Tictoc *generateNewMessage(int flag);
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

Define_Module(Tic);

void Tic::initialize()
{
    // Initialize variables.

    //window size
    size = par("size");

    seq = 0;
    timeout = .5;
    nxtmsg = .02;
    // flag if 1 send from queue or if 0 generate msg
    flag = 0;
    ackcount = 1;
    // Generate and send initial message.
    EV << "Sending initial control message from Tic\n";

    //control message
    msg1  = generateNewMessage(-1);
    send(msg1,"out");

    //timeout for control msg
    timeoutmsgEvent = generateNewMessage(4);
    scheduleAt(simTime()+timeout, timeoutmsgEvent);
}

void Tic::handleMessage(cMessage *msg)
{
    //convert cMessage to tictoc.msg
    Tictoc *ttmsg = check_and_cast<Tictoc *>(msg);

    //if the received message is a control message
    if (ttmsg->getMsgType() < 0)
    {
        cancelEvent(timeoutmsgEvent);
        //delete timeoutmsgEvent;

        nxtmsgEvent = generateNewMessage(3);
        scheduleAt(simTime()+nxtmsg, nxtmsgEvent);

        //no of msg for 1 ack
        ackcount = -ttmsg->getMsgType();
        count = ackcount;
    }

    //if the received timeout message for the control msg
    //resend control msg
    if (ttmsg->getMsgType() == 4)
    {
        cancelEvent(timeoutmsgEvent);
        delete timeoutmsgEvent;

        msg1 =  generateNewMessage(-1);
        send(msg1,"out");

        timeoutmsgEvent = generateNewMessage(4);
        scheduleAt(simTime()+timeout, timeoutmsgEvent);
    }

    //if the received timeout message for the msg
     if(ttmsg->getMsgType() == 0)
     {
         flag = 1;
         Tictoc *ttmsg = check_and_cast<Tictoc *>(msg);
         seq = ttmsg->getSeqNo() - size ;

         timeoutmsgEvent = generateNewMessage(0);
         scheduleAt(simTime()+timeout , timeoutmsgEvent);
     }

     //if NACK
     if(ttmsg->getMsgType() == 5)
     {
         flag = 1;
         Tictoc *ttmsg = check_and_cast<Tictoc *>(msg);
         seq = ttmsg->getSeqNo()-1;

         cancelEvent(timeoutmsgEvent);
         count = ttmsg->getSeqNo() + ackcount -1;

         while(ttmsg->getSeqNo() != seqmax+1-msgqueue.length())
             delete msgqueue.pop();
     }


     //if it is a self message time for next message
     //determines time between two consecutive msg
     if (ttmsg->getMsgType() == 3)
     {
     //send a new message at data rate and start a timer to the message
         cancelEvent(nxtmsgEvent);

         nxtmsgEvent = generateNewMessage(3);;
         scheduleAt(simTime()+nxtmsg, nxtmsgEvent);

         //check if there is available window size
         if(seq-count < (size-ackcount) )
         {
             if(seq == seqmax)
                 flag=0;

             if(flag==1)
             {
                 //resend msg if present in the queue

                 msg1 =(Tictoc *) msgqueue.pop();
                 cMessage *msgcpy = (cMessage *)msg1->dup();
                 send(msgcpy,"out");
                 ++seq;
                 msgqueue.insert(msg1);

                 cancelEvent(timeoutmsgEvent);
                 timeoutmsgEvent = generateNewMessage(0);
                 scheduleAt(simTime()+timeout , timeoutmsgEvent);
             }

             else
             {
                 //if not in queue generate a msg
                 if(seq < 255)
                 {
                     msg1 = generateNewMessage(1);
                     cMessage *msgcpy = (cMessage *)msg1->dup();
                     send(msgcpy,"out");
                     msgqueue.insert(msg1);
                 }

                 cancelEvent(timeoutmsgEvent);
                 timeoutmsgEvent = generateNewMessage(0);
                 scheduleAt(simTime()+timeout , timeoutmsgEvent);
             }
         }
     }

     // message arrived
     // Acknowledgment received!
     if(ttmsg->getMsgType() == 2)
     {
         if(count == ttmsg->getSeqNo() - 1 )
         {
             EV << "Received: " << ttmsg->getName() << "\n";
             //increase window size when ack is received
             count = count + ackcount;

             for (int i=0; i<ackcount;i++)
                 delete msgqueue.pop();

         }

         if(ttmsg->getSeqNo() == 256)
         {
             count = ackcount;
             seq = 0;
         }
     }
     delete msg;
}

Tictoc *Tic::generateNewMessage(int flag)
{
    // Generate a message with a different name every time.
    if(flag == 1)
    {
        char msgname[20];
        sprintf(msgname, "tic-%d", ++seq);


        Tictoc *msg = new Tictoc(msgname);
        msg->setSeqNo(seq);
        msg->setMsgType(1);
        seqmax = seq;
        return msg;
    }
    //Generate a message for a control message
    if(flag == -1)
    {
        Tictoc *msg = new Tictoc("TicControlMsg");
        msg->setSeqNo(size);
        msg->setMsgType(-1);
        return msg;
    }
    //Generate a self timer message
    if(flag == 0)
    {
        Tictoc *msg = new Tictoc("timeoutmsgEvent");
                msg->setSeqNo(seq);
                msg->setMsgType(0);
                return msg;
    }
    //Generate a next message timer
    if(flag == 3)
    {
        Tictoc *msg = new Tictoc("nxtmsgEvent");
        msg->setSeqNo(-1);
        msg->setMsgType(3);
        return msg;
    }
    //Generate a self timer message
    if(flag == 4)
    {
        Tictoc *msg = new Tictoc("timeoutcontrolEvent");
        msg->setSeqNo(-1);
        msg->setMsgType(4);
        return msg;
    }
}


/**
 * Sends back an acknowledgement -- or not.
 */
class Toc : public cSimpleModule
{
private:
    int  flag=0, seq=1,count1=0;
    cQueue queue ;
  protected:
    virtual void handleMessage(cMessage *msg) override;
};


Define_Module(Toc);

void Toc::handleMessage(cMessage *msg)
{
    //no. of msg for 1 ack
    static int ackcount = par("limit");

    Tictoc *ttmsg = check_and_cast<Tictoc *>(msg);
    //introducing msg losses
    if (uniform(0, 1) < 0.1)
    {
        EV << "\"Losing\" message " << msg << endl;
        bubble("message lost");
        delete msg;
    }
    else
    {
        queue.insert(ttmsg);
        //if received msg is control msg
        //send back control msg with ack size
        if (ttmsg->getMsgType() == -1)
        {
            Tictoc *msg1 = new Tictoc("TocControlMsg");

            msg1->setSeqNo(ttmsg->getSeqNo());

            //default ack size is 1
            //if ack size > window size
            if(ttmsg->getSeqNo() >= ackcount)
                msg1->setMsgType(-ackcount);
            else
            {
                msg1->setMsgType(-1);
                ackcount=1;
            }

            send(msg1, "out");
        }
        else
        {
            //ack
            if(ttmsg->getSeqNo() == seq)
            {
                seq++;
                if (flag==1)
                {
                    flag=0;
                    count1=0;
                }
                if( ttmsg->getSeqNo() == 255)
                {
                    Tictoc *msgack = new Tictoc("TocACKMsg");
                    msgack->setSeqNo(ttmsg->getSeqNo()+1);
                    msgack->setMsgType(2);
                    send(msgack, "out");
                }
                else
                {
                    if(count1== (ackcount-1))
                    {
                        Tictoc *msgack = new Tictoc("TocACKMsg");
                        msgack->setSeqNo(ttmsg->getSeqNo()+1);
                        msgack->setMsgType(2);
                        send(msgack, "out");
                        count1=0;
                    }
                    else
                        count1++;
                }
            }
            else
            {
                //negative ack
                count1 = 0;
                Tictoc *msgack = new Tictoc("TocNACKMsg");
                msgack->setSeqNo(seq);
                msgack->setMsgType(5);
                if(flag == 0 )
                    send(msgack, "out");
                flag = 1;
            }
            queue.pop();

            //reset after 255 msg
            if(ttmsg->getSeqNo() == 255)
            {
                flag=0, seq=1,count1=0;
            }
        }
    }
}




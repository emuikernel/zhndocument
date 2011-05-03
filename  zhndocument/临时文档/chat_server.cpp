#define _DLL
#define _RTLDLL
#define BOOST_DYN_LINK

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <set>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "chat_message.hpp"				// ��Ϣ����ʽ����

using boost::asio::ip::tcp;

//----------------------------------------------------------------------

typedef std::deque<chat_message> chat_message_queue;

//----------------------------------------------------------------------

//�����ҵĲ�����
class chat_participant
{
public:
  virtual ~chat_participant() {		}

  // ����
  virtual void deliver(const chat_message& msg) = 0;
};


// ����ָ�룺���ڹ�������߶���
typedef boost::shared_ptr<chat_participant> chat_participant_ptr;

//----------------------------------------------------------------------

// ������
class chat_room
{
public:

  /*
      *���ܣ� ����������
      *participant�� �����߶����ָ��
    */
  void join(chat_participant_ptr participant)
  {
    participants_.insert(participant);

    std::for_each(recent_msgs_.begin(), recent_msgs_.end(),
        boost::bind(&chat_participant::deliver, participant, _1));
  }

  // �뿪������
  void leave(chat_participant_ptr participant)
  {
    participants_.erase(participant);
  }

  // ����
  void deliver(const chat_message& msg)
  {
    recent_msgs_.push_back(msg);			// ��Ϣ�����	

    while (recent_msgs_.size() > max_recent_msgs)
	{
      recent_msgs_.pop_front();
	}

    std::for_each(participants_.begin(), participants_.end(),
        boost::bind(&chat_participant::deliver, _1, boost::ref(msg)));
  }

private:
  std::set<chat_participant_ptr> participants_;		// ���������в����ߵļ���

  enum { max_recent_msgs = 100 };					// �����Ϣ��

  chat_message_queue recent_msgs_;					// ��Ϣ����
};

//----------------------------------------------------------------------

class chat_session
  : public chat_participant,
    public boost::enable_shared_from_this<chat_session>
{
public:
  chat_session(boost::asio::io_service& io_service, chat_room& room)
    : socket_(io_service),
      room_(room)
  {
  }

  tcp::socket& socket()
  {
    return socket_;
  }

  void start()
  {
    room_.join(shared_from_this());		// ����������

	// Ͷ��һ���첽�����󡪶�ȡ��Ϣͷ
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.data(), chat_message::header_length),
        boost::bind(
          &chat_session::handle_read_header, shared_from_this(),
          boost::asio::placeholders::error));
  }

  // ����
  void deliver(const chat_message& msg)
  {
    bool write_in_progress = !write_msgs_.empty();

    write_msgs_.push_back(msg);

    if (!write_in_progress)
    {
		// Ͷ��һ���첽д����
      boost::asio::async_write(socket_,
          boost::asio::buffer(write_msgs_.front().data(),
            write_msgs_.front().length()),
          boost::bind(&chat_session::handle_write, shared_from_this(),
            boost::asio::placeholders::error));
    }
  }
  
  // �첽��ȡ������ɺ�Ļص���������ȡ��Ϣͷ
  void handle_read_header(const boost::system::error_code& error)
  {
    if (!error && read_msg_.decode_header())
    {
		// Ͷ��һ���첽�����󡪶�ȡ��Ϣ��
      boost::asio::async_read(socket_,				// I/O Object
		  boost::asio::buffer(read_msg_.body(),		
		  read_msg_.body_length()),
		  boost::bind(&chat_session::handle_read_body, shared_from_this(),
            boost::asio::placeholders::error));
    }
    else
    {
      room_.leave(shared_from_this());
    }
  }

  // �첽��ȡ������ɺ�Ļص���������ȡ��Ϣ��
  void handle_read_body(const boost::system::error_code& error)
  {
    if (!error)
    {
      room_.deliver(read_msg_);

	  // Ͷ��һ���첽�����󡪶�ȡ��Ϣͷ
      boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_.data(), chat_message::header_length),
          boost::bind(&chat_session::handle_read_header, shared_from_this(),
            boost::asio::placeholders::error));
    }
    else
    {
      room_.leave(shared_from_this());		// �뿪������
    }
  }

  
  void handle_write(const boost::system::error_code& error)
  {
    if (!error)
    {
      write_msgs_.pop_front();			// �����Ϣ���ѷ��ͣ�����

      if (!write_msgs_.empty())			// ��Ϣ����������Ϣ��
      {
		  // Ͷ��һ���첽д����
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(),
              write_msgs_.front().length()),
            boost::bind(&chat_session::handle_write, shared_from_this(),
              boost::asio::placeholders::error));
      }
    }
    else
    {
      room_.leave(shared_from_this());	 // �뿪������
    }
  }

private:
  tcp::socket socket_;					// socket����
  chat_room& room_;						// ������
  chat_message read_msg_;				// ��ȡ������Ϣ��
  chat_message_queue write_msgs_;
};


typedef boost::shared_ptr<chat_session> chat_session_ptr;


//----------------------------------------------------------------------

// �����ҷ�����
class chat_server
{
public:
  chat_server(boost::asio::io_service& io_service,
      const tcp::endpoint& endpoint)
    : io_service_(io_service),
      acceptor_(io_service, endpoint)
  {
    chat_session_ptr new_session(new chat_session(io_service_, room_));

	// Ͷ��һ���첽�������󡪽��ܿͻ�������
    acceptor_.async_accept(new_session->socket(),
        boost::bind(&chat_server::handle_accept, this, new_session,
          boost::asio::placeholders::error));
  }

  // �첽��������async_accept���ʱ���õĻص�����
  void handle_accept(chat_session_ptr session,
      const boost::system::error_code& error)
  {
    if (!error)
    {
      session->start();
      chat_session_ptr new_session(new chat_session(io_service_, room_));

	  // �ٴ�Ͷ����ͬ��async_accept���󣬲�ͣ�Ľ��ܿͻ��˵���������
      acceptor_.async_accept(new_session->socket(),
          boost::bind(&chat_server::handle_accept, this, new_session,
            boost::asio::placeholders::error));
    }
  }


private:
  boost::asio::io_service& io_service_;		// ǰ��������	
  tcp::acceptor acceptor_;					// ����������
  chat_room room_;							// ������

};

typedef boost::shared_ptr<chat_server> chat_server_ptr;
typedef std::list<chat_server_ptr> chat_server_list;



//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";

      return 1;
    }

    boost::asio::io_service io_service;		// ǰ��������

    chat_server_list servers;
    for (int i = 1; i < argc; ++i)
    {
      using namespace std;		// For atoi.

      tcp::endpoint endpoint(tcp::v4(), atoi(argv[i]));		// ���ؼ������׽��ֵ�ַ��Ϣ��IP��Port��

      chat_server_ptr server(new chat_server(io_service, endpoint));
      servers.push_back(server);
    }

    io_service.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

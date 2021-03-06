#include <ipc/client.h>
#include <ipc/server.h>

#include "assert.h"

#include <collector/system.h>
#include <deque>
#include <wpl/base/concepts.h>
#include <wpl/mt/synchronization.h>
#include <wpl/mt/thread.h>

#pragma warning(disable:4355)

namespace std
{
	using tr1::bind;
	using namespace tr1::placeholders;
}

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using namespace std;
using namespace wpl::mt;

namespace micro_profiler
{
	namespace ipc
	{
		namespace tests
		{
			namespace
			{
				class server;

				struct server_data
				{
					server_data()
						: session_count(0), changed_event(false, true)
					{	}

					int session_count;
					deque< vector<byte> > inputs;
					deque< vector<byte> > outputs;
					event_flag changed_event;
					mutex server_mutex;
				};


				class mock_session : public ipc::server::session, wpl::noncopyable
				{
				public:
					mock_session(const shared_ptr<server_data> &data);
					~mock_session();

					virtual void on_message(const vector<byte> &/*input*/, vector<byte> &output);

				private:
					const shared_ptr<server_data> _data;
				};


				class server : public ipc::server
				{
				public:
					server(const char *name)
						: ipc::server(name), data(new server_data), _thread(bind(&server::run, this))
					{	}

					~server()
					{
						stop();
						_thread.join();
					}

					void wait_for_session(int expected_count)
					{
						for (;; data->changed_event.wait())
						{
							scoped_lock l(data->server_mutex);

							if (data->session_count == expected_count)
								return;
						}
					}

					void add_output(const vector<byte> &data)
					{
						scoped_lock l(this->data->server_mutex);

						this->data->outputs.push_back(data);
					}

					deque< vector<byte> > get_inputs()
					{
						scoped_lock l(data->server_mutex);

						return data->inputs;
					}

					shared_ptr<server_data> data;

				private:
					virtual session *create_session()
					{	return new mock_session(data);	}

				private:
					thread _thread;
				};



				mock_session::mock_session(const shared_ptr<server_data> &data)
					: _data(data)
				{
					scoped_lock l(_data->server_mutex);
						
					++_data->session_count;
					data->changed_event.raise();
				}

				mock_session::~mock_session()
				{
					scoped_lock l(_data->server_mutex);
						
					--_data->session_count;
					_data->changed_event.raise();
				}

				void mock_session::on_message(const vector<byte> &input, vector<byte> &output)
				{
					scoped_lock l(_data->server_mutex);

					_data->inputs.push_back(input);
					output = _data->outputs.front();
					_data->outputs.pop_front();
				}


				template <typename T, size_t size>
				vector<T> mkvector(T (&array_ptr)[size])
				{	return vector<T>(array_ptr, array_ptr + size);	}
			}

			[TestClass]
			public ref class IPCClientServerTests
			{
			public: 
				[TestMethod]
				void NoServersAreListedIfNoneIsRunning()
				{
					// INIT
					vector<string> servers;

					// ACT
					client::enumerate_servers(servers);

					// ASSERT
					Assert::IsTrue(servers.empty());
				}


				[TestMethod]
				void CreatedServersAreListed()
				{
					// INIT
					vector<string> servers;

					// ACT
					server s1("foo");
					client::enumerate_servers(servers);

					// ASSERT
					const string reference1[] = { "foo", };

					assert::sequences_equivalent(reference1, servers);

					// ACT
					server s2("Foe X");
					client::enumerate_servers(servers);

					// ASSERT
					const string reference2[] = { "foo", "Foe X", };

					assert::sequences_equivalent(reference2, servers);

					// ACT
					server s3("Bar Z"), s4("hope.this.will.be.visible");
					client::enumerate_servers(servers);

					// ASSERT
					const string reference3[] = { "foo", "Foe X", "Bar Z", "hope.this.will.be.visible", };

					assert::sequences_equivalent(reference3, servers);
				}


				[TestMethod]
				void DeletedServersAreRemovedFromList()
				{
					// INIT
					vector<string> servers;
					auto_ptr<server> s1(new server("foo2")), s2(new server("bar")), s3(new server("bzzz. . .p"));

					// ACT
					s2.reset();
					client::enumerate_servers(servers);

					// ASSERT
					const string reference1[] = { "foo2", "bzzz. . .p", };

					assert::sequences_equivalent(reference1, servers);

					// ACT
					s1.reset();
					client::enumerate_servers(servers);

					// ASSERT
					const string reference2[] = { "bzzz. . .p", };

					assert::sequences_equivalent(reference2, servers);
				}


				[TestMethod]
				void ClientCreationLeadsToSessionCreation()
				{
					// INIT
					server s("test");

					// ACT / ASSERT (must not hang)
					client c1("test");
					s.wait_for_session(1);
				}


				[TestMethod]
				void MultipleConnectionsCreateMultipleSessions()
				{
					// INIT
					server s("test2");

					// ACT / ASSERT (must not hang)
					client c1("test2"), c2("test2"), c3("test2");
					s.wait_for_session(3);

					// ACT / ASSERT (must not hang)
					client c4("test2"), c5("test2");
					s.wait_for_session(5);
				}


				[TestMethod]
				void ClosingClientConnectionDropsSessions()
				{
					// INIT
					server s("test");
					auto_ptr<client> c1(new client("test")), c2(new client("test")), c3(new client("test"));

					s.wait_for_session(3);

					// ACT / ASSERT (must not hang)
					c2.reset();
					s.wait_for_session(2);

					c1.reset();
					c3.reset();
					s.wait_for_session(0);
				}


				[TestMethod]
				void ClientReadsServersOutputs()
				{
					// INIT
					server s("test-1");

					byte m1[] = "hello!";
					byte m2[] = "how are you?";
					byte m3[] = "bye!";

					s.add_output(mkvector(m1));
					s.add_output(mkvector(m2));
					s.add_output(mkvector(m3));

					client c("test-1");
					vector<byte> i, o;

					// ACT / ASSERT (must not hang)
					c.call(i, o);

					// ASSERT
					assert::sequences_equivalent(m1, o);

					// ACT
					c.call(i, o);

					// ASSERT
					assert::sequences_equivalent(m2, o);

					// ACT
					c.call(i, o);

					// ASSERT
					assert::sequences_equivalent(m3, o);
				}


				[TestMethod]
				void ServerReadsClientsInputs()
				{
					// INIT
					server s("test-2");

					byte m1[] = "hello!";
					byte m2[] = "how are you?";
					byte m3[] = "bye!";

					s.data->outputs.resize(3);

					client c("test-2");
					vector<byte> o;
					deque< vector<byte> > inputs;


					// ACT
					c.call(mkvector(m1), o);
					c.call(mkvector(m3), o);

					// ASSERT
					inputs = s.get_inputs();

					Assert::AreEqual(2u, inputs.size());
					assert::sequences_equivalent(m1, inputs[0]);
					assert::sequences_equivalent(m3, inputs[1]);

					// ACT
					c.call(mkvector(m2), o);

					// ASSERT
					inputs = s.get_inputs();

					Assert::AreEqual(3u, inputs.size());
					assert::sequences_equivalent(m2, inputs[2]);
				}


				[TestMethod]
				void ServerCanReadLongMessagesFromClients()
				{
					// INIT
					server s("test-3");
					s.data->outputs.resize(3);
					deque< vector<byte> > inputs;
					client c("test-3");
					vector<byte> i, o;

					i.resize(122131);
					i[3] = 89;
					i[30101] = 190;
					i[122130] = 17;

					// ACT
					c.call(i, o);

					// ASSERT
					inputs = s.get_inputs();
					Assert::AreEqual(1u, inputs.size());
					assert::sequences_equivalent(i, inputs[0]);

					// INIT
					i.resize(13739);
					i[3] = 89;
					i[7122] = 191;
					i[13738] = 19;

					// ACT
					c.call(i, o);

					// ASSERT
					inputs = s.get_inputs();
					Assert::AreEqual(2u, inputs.size());
					assert::sequences_equivalent(i, inputs[1]);
				}


				[TestMethod]
				void ClientCanReadLongServerReplies()
				{
					// INIT
					vector<byte> i, o, tmp;
					server s("test-3");
					client c("test-3");

					tmp.resize(122133);
					tmp[3] = 79;
					tmp[3011] = 90;
					tmp[122121] = 171;
					s.add_output(tmp);

					// ACT
					c.call(i, o);

					// ASSERT
					assert::sequences_equivalent(tmp, o);

					// INIT
					tmp.resize(7356);
					tmp[3] = 89;
					tmp[7100] = 191;
					s.add_output(tmp);

					// ACT
					c.call(i, o);

					// ASSERT
					assert::sequences_equivalent(tmp, o);
				}
			};
		}
	}
}

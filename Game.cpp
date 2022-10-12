#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#include <glm/gtx/norm.hpp>
#include <glm/gtx/string_cast.hpp>

void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 5) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");
	
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4+0], &left);
	recv_button(recv_buffer[4+1], &right);
	recv_button(recv_buffer[4+2], &up);
	recv_button(recv_buffer[4+3], &down);
	recv_button(recv_buffer[4+4], &jump);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------

Game::Game() : mt(0x15466666) {
	// horizontal
	{
		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(-0.2f, 0.3f);
		platforms.back().positionMax = glm::vec2(0.2f, 0.4f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(-1.3f, -0.5f);
		platforms.back().positionMax = glm::vec2(-0.8f, -0.4f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(0.6f, 0.3f);
		platforms.back().positionMax = glm::vec2(1.2f, 0.4f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(1.0f, -0.7f);
		platforms.back().positionMax = glm::vec2(1.4f, -0.6f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(0.0f, -0.7f);
		platforms.back().positionMax = glm::vec2(0.4f, -0.6f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(-1.0f, -0.1f);
		platforms.back().positionMax = glm::vec2(-0.6f, 0.0f);
	}

	// vertical
	{
		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(-0.8f, 0.3f);
		platforms.back().positionMax = glm::vec2(-0.7f, 0.8f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(0.7f, -1.0f);
		platforms.back().positionMax = glm::vec2(0.8f, -0.5f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(0.5f, 0.6f);
		platforms.back().positionMax = glm::vec2(0.6f, 0.9f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(1.0f, -0.3f);
		platforms.back().positionMax = glm::vec2(1.1f, 0.2f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(-0.5f, -0.7f);
		platforms.back().positionMax = glm::vec2(-0.4f, -0.2f);

		platforms.emplace_back();
		platforms.back().positionMin = glm::vec2(-0.1f, -0.3f);
		platforms.back().positionMax = glm::vec2(0.0f, 0.1f);
	}
}

Player *Game::spawn_player() {
	players.emplace_back();
	Player &player = players.back();

	//random point in the middle area of the arena:
	player.position.x = glm::mix(ArenaMin.x + 2.0f * PlayerRadius, ArenaMax.x - 2.0f * PlayerRadius, 0.4f + 0.2f * mt() / float(mt.max()));
	player.position.y = glm::mix(ArenaMin.y + 2.0f * PlayerRadius, ArenaMax.y - 2.0f * PlayerRadius, 0.4f + 0.2f * mt() / float(mt.max()));
	player.position.y = ArenaMin.y + 2.0f * PlayerRadius;

	do {
		player.color.r = mt() / float(mt.max());
		player.color.g = mt() / float(mt.max());
		player.color.b = mt() / float(mt.max());
	} while (player.color == glm::vec3(0.0f));
	player.color = glm::normalize(player.color);

	player.name = "Player " + std::to_string(next_player_number++);

	return &player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi) {
		if (&*pi == player) {
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

// https://lazyfoo.net/tutorials/SDL/27_collision_detection/index.php
bool Game::check_collision(float leftA, float leftB, float rightA, float rightB, float topA, float topB, float bottomA, float bottomB) {
	//If any of the sides from A are outside of B
    if (bottomA >= topB) {
        return false;
    }

    if (topA <= bottomB) {
        return false;
    }

    if (rightA <= leftB) {
        return false;
    }

    if (leftA >= rightB) {
        return false;
    }

    //If none of the sides from A are outside B
    return true;
}

void Game::update(float elapsed) {
	//position/velocity update:
	for (auto &p : players) {
		if (p.controls.jump.pressed && !p.jump_pressing) {
			if (p.gravity < 0.0f) {
				p.acceleration = 3.0f; // have to be opposite sign as gravity
			} else {
				p.acceleration = -3.0f;
			}
			p.jump_pressing = true;
		} else if (!p.controls.jump.pressed) {
			p.jump_pressing = false;
		}

		if (p.movement_index == 0) {
			p.position.y += p.acceleration * elapsed;
			bool collide = false;
			float leftA = p.position.x - PlayerRadius;
			float rightA = p.position.x + PlayerRadius;
			float topA = p.position.y + PlayerRadius;
			float bottomA = p.position.y - PlayerRadius;
			for (auto &platform : platforms) {
				float leftB = platform.positionMin.x;
				float rightB = platform.positionMax.x;
				float topB = platform.positionMax.y;
				float bottomB = platform.positionMin.y;
				if (check_collision(leftA, leftB, rightA, rightB, topA, topB, bottomA, bottomB)) {
					collide = true;
					p.position.y -= p.acceleration * elapsed;
					if (p.gravity < 0.0f) {
						if (p.position.y < bottomB - PlayerRadius) {
							p.position.y = bottomB - PlayerRadius - 0.001f;
						} else {
							p.position.y = topB + PlayerRadius + 0.001f;
						}
					} else {
						if (p.position.y > topB + PlayerRadius) {
							p.position.y = topB + PlayerRadius + 0.001f;
						} else {
							p.position.y = bottomB - PlayerRadius - 0.001f;
						}
					}
					p.acceleration = 0.0f;
				}
			}
			if (!collide) {
				p.acceleration += p.gravity * elapsed;
			}

			if (p.controls.left.pressed && !p.controls.right.pressed) {
				p.position.x -= p.velocity.x * elapsed;
			} else if (!p.controls.left.pressed && p.controls.right.pressed) {
				p.position.x += p.velocity.x * elapsed;
			} 
		} else {
			p.position.x += p.acceleration * elapsed;
			bool collide = false;
			float leftA = p.position.x - PlayerRadius;
			float rightA = p.position.x + PlayerRadius;
			float topA = p.position.y + PlayerRadius;
			float bottomA = p.position.y - PlayerRadius;
			for (auto &platform : platforms) {
				float leftB = platform.positionMin.x;
				float rightB = platform.positionMax.x;
				float topB = platform.positionMax.y;
				float bottomB = platform.positionMin.y;
				if (check_collision(leftA, leftB, rightA, rightB, topA, topB, bottomA, bottomB)) {
					collide = true;
					p.position.x -= p.acceleration * elapsed;
					if (p.gravity < 0.0f) {
						if (p.position.x < leftB - PlayerRadius) {
							p.position.x = leftB - PlayerRadius - 0.001f;
						} else {
							p.position.x = rightB + PlayerRadius + 0.001f;
						}
					} else {
						if (p.position.x > rightB + PlayerRadius) {
							p.position.x = rightB + PlayerRadius + 0.001f;
						} else {
							p.position.x = leftB - PlayerRadius - 0.001f;
						}
					}
					
					p.acceleration = 0.0f;
				}
			}
			if (!collide) {
				p.acceleration += p.gravity * elapsed;
			}
			
			if (p.controls.down.pressed && !p.controls.up.pressed) {
				p.position.y -= p.velocity.y * elapsed;
			} else if (!p.controls.down.pressed && p.controls.up.pressed) {
				p.position.y += p.velocity.y * elapsed;
			} 
		}

		//reset 'downs' since controls have been handled:
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
		p.controls.jump.downs = 0;
		p.controls.jump.pressed = false;
	}

	//collision resolution:
	for (auto &p1 : players) {
		// //player/player collisions:
		// for (auto &p2 : players) {
		// 	if (&p1 == &p2) break;
		// 	glm::vec2 p12 = p2.position - p1.position;
		// 	float len2 = glm::length2(p12);
		// 	if (len2 > (2.0f * PlayerRadius) * (2.0f * PlayerRadius)) continue;
		// 	if (len2 == 0.0f) continue;
		// 	glm::vec2 dir = p12 / std::sqrt(len2);
		// 	//mirror velocity to be in separating direction:
		// 	glm::vec2 v12 = p2.velocity - p1.velocity;
		// 	glm::vec2 delta_v12 = dir * glm::max(0.0f, -1.75f * glm::dot(dir, v12));
		// 	p2.velocity += 0.5f * delta_v12;
		// 	p1.velocity -= 0.5f * delta_v12;
		// }

		//player/block collisions:
		float leftA = p1.position.x - PlayerRadius;
		float rightA = p1.position.x + PlayerRadius;
		float topA = p1.position.y + PlayerRadius;
		float bottomA = p1.position.y - PlayerRadius;
		for (auto &platform : platforms) {
			float leftB = platform.positionMin.x;
			float rightB = platform.positionMax.x;
			float topB = platform.positionMax.y;
			float bottomB = platform.positionMin.y;
			if (check_collision(leftA, leftB, rightA, rightB, topA, topB, bottomA, bottomB)) {
				if (p1.movement_index == 0) {
					if (p1.controls.left.pressed && !p1.controls.right.pressed) {
						p1.position.x += p1.velocity.x * elapsed;
						if (p1.position.x > rightB + PlayerRadius) {
							p1.position.x = rightB + PlayerRadius;
						}
					} else if (!p1.controls.left.pressed && p1.controls.right.pressed) {
						p1.position.x -= p1.velocity.x * elapsed;
						if (p1.position.x < leftB - PlayerRadius) {
							p1.position.x = leftB - PlayerRadius;
						}
					} 
				} else {
					if (p1.controls.down.pressed && !p1.controls.up.pressed) {
						p1.position.y += p1.velocity.y * elapsed;
						if (p1.position.y > topB + PlayerRadius) {
							p1.position.y = topB + PlayerRadius;
						}
					} else if (!p1.controls.down.pressed && p1.controls.up.pressed) {
						p1.position.y -= p1.velocity.y * elapsed;
						if (p1.position.y < bottomB - PlayerRadius) {
							p1.position.y = bottomB - PlayerRadius;
						}
					} 
				}
			}
		}

		//player/arena collisions:
		if (p1.position.x < ArenaMin.x + PlayerRadius) {
			p1.position.x = ArenaMin.x + PlayerRadius;
			if (p1.movement_index == 1) {
				p1.acceleration = 0.0f;
			}
		}
		if (p1.position.x > ArenaMax.x - PlayerRadius) {
			p1.position.x = ArenaMax.x - PlayerRadius;
			if (p1.movement_index == 1) {
				p1.acceleration = 0.0f;
			}
		}
		if (p1.position.y < ArenaMin.y + PlayerRadius) {
			p1.position.y = ArenaMin.y + PlayerRadius;
			if (p1.movement_index == 0) {
				p1.acceleration = 0.0f;
			}
		}
		if (p1.position.y > ArenaMax.y - PlayerRadius) {
			p1.position.y = ArenaMax.y - PlayerRadius;
			if (p1.movement_index == 0) {
				p1.acceleration = 0.0f;
			}
		}
	}

}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer


	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.position);
		connection.send(player.velocity);
		connection.send(player.color);
	
		//NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		//effectively: truncates player name to 255 chars
		uint8_t len = uint8_t(std::min< size_t >(255, player.name.size()));
		connection.send(len);
		connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);
	};

	//player count:
	connection.send(uint8_t(players.size()));
	if (connection_player) send_player(*connection_player);
	for (auto const &player : players) {
		if (&player == connection_player) continue;
		send_player(player);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.position);
		read(&player.velocity);
		read(&player.color);
		uint8_t name_len;
		read(&name_len);
		//n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
		player.name = "";
		for (uint8_t n = 0; n < name_len; ++n) {
			char c;
			read(&c);
			player.name += c;
		}
	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}

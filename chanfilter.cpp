/*
 * Copyright (C) 2004-2014  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/Nick.h>

class CChanFilterTimer : public CTimer
{
public:
	CChanFilterTimer(CModule* module, const CString& identifier, const CString& channel)
		: CTimer(module, 5, 1, "ChanFilter/" + identifier + "/" + channel, ""), m_identifier(identifier), m_channel(channel) { }

	virtual void RunJob() override;

private:
	CString m_identifier;
	CString m_channel;
};

class CChanFilterMod : public CModule
{
public:
	MODCONSTRUCTOR(CChanFilterMod)
	{
		AddHelpCommand();
		AddCommand("AddClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnAddClientCommand), "<identifier>", "Add a client.");
		AddCommand("DelClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnDelClientCommand), "<identifier>", "Delete a client.");
		AddCommand("ListClients", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnListClientsCommand), "", "List known clients and their hidden channels.");
		AddCommand("ListChans", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnListChansCommand), "[client]", "List all channels of a client.");
		AddCommand("JoinChans", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnJoinChansCommand), "[client]", "Join the hidden channels of a client.");
	}

	void OnAddClientCommand(const CString& line);
	void OnDelClientCommand(const CString& line);
	void OnListClientsCommand(const CString& = "");
	void OnListChansCommand(const CString& line);
	void OnJoinChansCommand(const CString& line);

	virtual void OnClientLogin() override;

	virtual EModRet OnUserRaw(CString& line) override;
	virtual EModRet OnSendToClient(CString& line, CClient& client) override;

	SCString GetHiddenChannels(const CString& identifier) const;
	bool IsChannelVisible(const CString& identifier, const CString& channel) const;
	void SetChannelVisible(const CString& identifier, const CString& channel, bool visible);

	bool AddClient(const CString& identifier);
	bool DelClient(const CString& identifier);
	std::vector<CClient*> FindClients(const CString& identifier) const;
};

void CChanFilterTimer::RunJob()
{
	CChanFilterMod* module = static_cast<CChanFilterMod*>(GetModule());
	if (module) {
		CIRCNetwork* network = module->GetNetwork();
		if (network) {
			CClient* client = network->FindClient(m_identifier);
			if (client)
				module->SetChannelVisible(m_identifier, m_channel, false);
		}
	}
}

void CChanFilterMod::OnAddClientCommand(const CString& line)
{
	const CString identifier = line.Token(1);
	if (!AddClient(identifier)) {
		PutModule("Usage: AddClient <identifier>");
		return;
	}
	PutModule("Client added: " + identifier);
}

void CChanFilterMod::OnDelClientCommand(const CString& line)
{
	const CString identifier = line.Token(1);
	if (!DelClient(identifier)) {
		PutModule("Usage: DelClient <identifier>");
		return;
	}
	PutModule("Client removed: " + identifier);
}

void CChanFilterMod::OnListClientsCommand(const CString&)
{
	const CString current = GetClient()->GetIdentifier();

	CTable table;
	table.AddColumn("Client");
	table.AddColumn("Connected");
	table.AddColumn("Hidden channels");
	for (MCString::iterator it = BeginNV(); it != EndNV(); ++it) {
		table.AddRow();
		if (it->first == current)
			table.SetCell("Client",  "*" + it->first);
		else
			table.SetCell("Client",  it->first);
		table.SetCell("Connected", CString(GetNetwork()->FindClient(it->first)));
		table.SetCell("Hidden channels", it->second.Ellipsize(128));
	}

	if (table.empty())
		PutModule("No identified clients");
	else
		PutModule(table);
}

void CChanFilterMod::OnListChansCommand(const CString& line)
{
	CString identifier = line.Token(1);
	if (identifier.empty())
		identifier = GetClient()->GetIdentifier();

	if (identifier.empty()) {
		PutModule("Unidentified client");
		return;
	}

	if (FindNV(identifier) == EndNV()) {
		PutModule("Unknown client: " + identifier);
		return;
	}

	CTable table;
	if (!identifier.empty())
		table.AddColumn("Client");
	table.AddColumn("Channel");
	table.AddColumn("Status");

	for (CChan* channel : GetNetwork()->GetChans()) {
		table.AddRow();
		if (!identifier.empty())
			table.SetCell("Client", identifier);
		table.SetCell("Channel", channel->GetName());
		if (channel->IsDisabled())
			table.SetCell("Status", "Disabled");
		else if (channel->IsDetached())
			table.SetCell("Status", "Detached");
		else if (identifier.empty() || IsChannelVisible(identifier, channel->GetName()))
			table.SetCell("Status", "Visible");
		else
			table.SetCell("Status", "Hidden");
	}

	PutModule(table);
}

void CChanFilterMod::OnJoinChansCommand(const CString& line)
{
	CString identifier = line.Token(1);
	if (identifier.empty())
		identifier = GetClient()->GetIdentifier();

	if (identifier.empty()) {
		PutModule("Unidentified client");
		return;
	}

	if (FindNV(identifier) == EndNV()) {
		PutModule("Unknown client: " + identifier);
		return;
	}

	const SCString channels = GetHiddenChannels(identifier);
	if (channels.empty()) {
		PutModule("No hidden channels");
		return;
	}

	unsigned int count = 0;
	for (const CString& name : channels) {
		SetChannelVisible(identifier, name, true);
		CChan* channel = GetNetwork()->FindChan(name);
		if (channel) {
			for (CClient* client : FindClients(identifier))
				channel->JoinUser(true, "", client);
			++count;
		}
	}
	PutModule("Joined " + CString(count) + " channels");
}

void CChanFilterMod::OnClientLogin()
{
	AddClient(GetClient()->GetIdentifier());
}

CModule::EModRet CChanFilterMod::OnUserRaw(CString& line)
{
	CClient* client = GetClient();
	const CString identifier = client->GetIdentifier();

	if (!identifier.empty()) {
		const CString cmd = line.Token(0);
		if (cmd.Equals("JOIN")) {
			const CString name = line.Token(1);
			SetChannelVisible(identifier, name, true);
			CChan* channel = client->GetNetwork()->FindChan(name);
			if (channel) {
				for (CClient* cli : FindClients(identifier))
					channel->JoinUser(true, "", cli);
				return HALT;
			}
		} else if (cmd.Equals("PART")) {
			const CString channel = line.Token(1);
			AddTimer(new CChanFilterTimer(this, identifier, channel));
			for (CClient* cli : FindClients(identifier)) {
				// bypass OnUserRaw()
				cli->Write(":" + cli->GetNickMask() + " PART " + channel + "\r\n");
			}
			return HALT;
		}
	}
	return CONTINUE;
}

CModule::EModRet CChanFilterMod::OnSendToClient(CString& line, CClient& client)
{
	EModRet ret = CONTINUE;
	CIRCNetwork* network = client.GetNetwork();
	CString identifier = client.GetIdentifier();

	if (network && !identifier.empty()) {
		// discard message tags
		CString msg = line;
		if (msg.StartsWith("@"))
			msg = msg.Token(1, true);

		const CNick nick(msg.Token(0).TrimPrefix_n());
		const CString cmd = msg.Token(1);
		const CString rest = msg.Token(2, true);

		CString channel;
		if (cmd.length() == 3 && isdigit(cmd[0]) && isdigit(cmd[1]) && isdigit(cmd[2])) {
			unsigned int num = cmd.ToUInt();
			if (num == 353) // RPL_NAMES
				channel = rest.Token(2);
			else
				channel = rest.Token(1);
		} else if (cmd.Equals("NOTICE")) {
			if (nick.NickEquals("ChanServ")) {
				CString target = rest.Token(1).TrimPrefix_n(":[").TrimSuffix_n("]");
				if (network->IsChan(target) && !IsChannelVisible(identifier, target))
					return HALT;
			}
			channel = rest.Token(0);
		} else if (cmd.Equals("PRIVMSG") || cmd.Equals("JOIN") || cmd.Equals("PART") || cmd.Equals("MODE") || cmd.Equals("KICK") || cmd.Equals("TOPIC")) {
			channel = rest.Token(0);
		}
		channel.TrimPrefix(":");

		if (network->IsChan(channel) && !IsChannelVisible(identifier, channel))
			ret = HALT;

		if (cmd.Equals("PART") && client.IsConnected() && !client.IsClosed() && nick.GetNick().Equals(client.GetNick()))
			SetChannelVisible(identifier, channel, true);
	}
	return ret;
}

SCString CChanFilterMod::GetHiddenChannels(const CString& identifier) const
{
	SCString channels;
	GetNV(identifier).Split(",", channels);
	return channels;
}

bool CChanFilterMod::IsChannelVisible(const CString& identifier, const CString& channel) const
{
	const SCString channels = GetHiddenChannels(identifier);
	return channels.find(channel.AsLower()) == channels.end();
}

void CChanFilterMod::SetChannelVisible(const CString& identifier, const CString& channel, bool visible)
{
	if (!identifier.empty()) {
		SCString channels = GetHiddenChannels(identifier);
		if (visible)
			channels.erase(channel.AsLower());
		else
			channels.insert(channel.AsLower());
		SetNV(identifier, CString(",").Join(channels.begin(), channels.end()));
	}
}

bool CChanFilterMod::AddClient(const CString& identifier)
{
	if (!identifier.empty()) {
		SetNV(identifier, GetNV(identifier));
		return true;
	}
	return false;
}

bool CChanFilterMod::DelClient(const CString& identifier)
{
	if (!identifier.empty()) {
		DelNV(identifier);
		return true;
	}
	return false;
}

// TODO: CIRCNetwork::FindClients()
std::vector<CClient*> CChanFilterMod::FindClients(const CString& identifier) const
{
	std::vector<CClient*> clients;
	for (CClient* client : GetNetwork()->GetClients()) {
		if (client->GetIdentifier().Equals(identifier))
			clients.push_back(client);
	}
	return clients;
}

template<> void TModInfo<CChanFilterMod>(CModInfo& Info)
{
	Info.SetWikiPage("chanfilter");
}

NETWORKMODULEDEFS(CChanFilterMod, "A channel filter module for ZNC")

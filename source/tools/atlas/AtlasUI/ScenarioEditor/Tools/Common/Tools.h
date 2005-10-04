#ifndef TOOLS_H__
#define TOOLS_H__

#include "ScenarioEditor/ScenarioEditor.h"
#include "general/AtlasWindowCommand.h"

class wxMouseEvent;
class wxKeyEvent;

class ITool
{
public:
	enum KeyEventType { KEY_DOWN, KEY_UP, KEY_CHAR };

	virtual void OnMouse(wxMouseEvent& evt) = 0;
	virtual void OnKey(wxKeyEvent& evt, KeyEventType dir) = 0;
	virtual void OnTick(float dt) = 0; // dt in seconds

	virtual ~ITool() {};
};

#define DECLARE_TOOL(name) ITool* CreateTool_##name() { return new name(); }


#define USE_TOOL(name) { extern ITool* CreateTool_##name(); SetCurrentTool(CreateTool_##name()); }

extern ITool* g_CurrentTool;
extern void SetCurrentTool(ITool*); // for internal use only

//////////////////////////////////////////////////////////////////////////


namespace AtlasMessage { struct mWorldCommand; }
class WorldCommand : public AtlasWindowCommand
{
	DECLARE_CLASS(WorldCommand);

	bool m_AlreadyDone;
public:
	WorldCommand(AtlasMessage::mWorldCommand* command);
	~WorldCommand();
	bool Do();
	bool Undo();
	bool Merge(AtlasWindowCommand* previousCommand);

private:
	AtlasMessage::mWorldCommand* m_Command;
};

#define ADD_WORLDCOMMAND(type, data) ScenarioEditor::GetCommandProc().Submit(new WorldCommand(new AtlasMessage::m##type(AtlasMessage::d##type data)))

//////////////////////////////////////////////////////////////////////////

#define SET_STATE(s) obj->SetState(&obj->s)

template <typename T>
class StateDrivenTool : public ITool
{
public:
	StateDrivenTool()
		: m_CurrentState(&Disabled)
	{
	}

	~StateDrivenTool()
	{
		SetState(&Disabled);
	}

protected:
	// Called when the tool is enabled/disabled; always called in zero or
	// more enable-->disable pairs per object instance.
	virtual void OnEnable(T* WXUNUSED(obj)) {}
	virtual void OnDisable(T* WXUNUSED(obj)) {}

	struct State
	{
		virtual void OnEnter(T* WXUNUSED(obj)) {}
		virtual void OnLeave(T* WXUNUSED(obj)) {}
		virtual void OnTick (T* WXUNUSED(obj), float WXUNUSED(dt)) {}

		// Should return true if the event has been handled (else the event will
		// be passed to a lower-priority level)
		virtual bool OnMouse(T* WXUNUSED(obj), wxMouseEvent& WXUNUSED(evt)) { return false; }
		virtual bool OnKey(T* WXUNUSED(obj), wxKeyEvent& WXUNUSED(evt), KeyEventType WXUNUSED(type)) { return false; }
	};


	struct sDisabled : public State
	{
		void OnEnter(T* obj) { obj->OnDisable(obj); }
		void OnLeave(T* obj) { obj->OnEnable(obj); }
	}
	Disabled;

	void SetState(State* state)
	{
		m_CurrentState->OnLeave(static_cast<T*>(this));
		// this cast is safe as long as the class is used as in
		// "class Something : public StateDrivenTool<Something> { ... }"
		m_CurrentState = state;
		m_CurrentState->OnEnter(static_cast<T*>(this));
	}
private:
	State* m_CurrentState;

	virtual void OnMouse(wxMouseEvent& evt)
	{
		m_CurrentState->OnMouse(static_cast<T*>(this), evt);
	}

	virtual void OnKey(wxKeyEvent& evt, KeyEventType dir)
	{
		m_CurrentState->OnKey(static_cast<T*>(this), evt, dir);
	}

	virtual void OnTick(float dt)
	{
		m_CurrentState->OnTick(static_cast<T*>(this), dt);
	}
};


#endif // TOOLS_H__

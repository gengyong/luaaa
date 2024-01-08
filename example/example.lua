

luaaa = {}
function luaaa:extend(base, obj)
	derived = obj or {}
	derived.new = function(self, ...)
		o = base.new(...)
		setmetatable(self, getmetatable(o))
		self["@"] = o
		return self
	end
	return derived
end

function luaaa:base(obj)
	if (type(obj) == "table") then
		return obj["@"]
	end
	return nil
end


function serialize(obj)
	local str = ""
	local t = type(obj)
	if t == "number" then
		str = str .. obj
	elseif t == "boolean" then
		str = str .. tostring(obj)
	elseif t == "string" then
		str = str ..  obj
	elseif t == "table" then
		str = str .. "{"
		for k, v in pairs(obj) do
			str = str .. "[" .. serialize(k) .. "]=" .. serialize(v) .. ","
		end
		local metatable = getmetatable(obj)
		if metatable ~= nil and type(metatable.__index) == "table" then
			for k, v in pairs(metatable.__index) do  
				str = str .. "[" .. serialize(k) .. "]=" .. serialize(v) .. ","
			end
		end
		str = str .. "}"
	elseif t == "nil" then
		str = "nil"
	else  
		error("can not serialize a " .. t .. " type.")
	end  
	return str  
end

function luaCallback(param)
	return param + 1
end

function testAwesomeCat()
	local a = AwesomeCat.new ("BINGO")
	for key,value in pairs(getmetatable(a)) do
		print(key, value)
	end

	a:setAge(2);
	print(a)
	a:eat({"fish", "milk", "cookie", "rice"});
	print(a)
	print("cat test: send params to c++: (0:0, 1:1, 2:2, 3:3, 4:4)");
	a:test(0, 1, 2, 3, 4)
	a:speak("Thanks!")
	if not WITHOUT_CPP_STDLIB then
		print(a:testFunctor1(99999, 88888))
		print(a:testFunctor2(77777, 66666))
		a:testfunctor(luaCallback)
	end

	a.say = "I am a cat!";
	a.age = 12
	print("=================== a.age:",  a.age)
	a.name  = "HeroCat";
	print("=================== a.name:", a.name)
	a.prop1 = "white";
	print("=================== a.prop1:", a.prop1)
	if not WITHOUT_CPP_STDLIB then
		a.prop2 = 9999123;
		print("=================== a.prop2:", a.prop2)
		a.prop3 = { 'aaa', 'bbb', 'ccc', 'ddd' };
		print("=================== a.prop3:", serialize(a.prop3))
		a.prop4 = 1.234;
		print("=================== a.prop3:", a.prop4)
	end
	
end

function testAwesomeMod()
	print("===================before assign AwesomeMod.notexists:", AwesomeMod.notexists2)
	AwesomeMod.notexists2 = "asdadsasda";
	print("=================== after assign AwesomeMod.notexists:", AwesomeMod.notexists2)
	
	AwesomeMod.prop1 = "white";
	print("=================== AwesomeMod.prop1:", AwesomeMod.prop1)
	if not WITHOUT_CPP_STDLIB then
		AwesomeMod.prop2 = 9999123;
		print("=================== AwesomeMod.prop2:", AwesomeMod.prop2)
		AwesomeMod.prop3 = "abcdefg";
		print("=================== AwesomeMod.prop3:", serialize(AwesomeMod.prop3))
		AwesomeMod.prop4 = 1.234;
		print("=================== AwesomeMod.prop4:", AwesomeMod.prop4)
	end

	print ("AwesomeMod.cint:" .. AwesomeMod.cint)
	print ("AwesomeMod.cstr:" .. AwesomeMod.cstr)
	print ("AwesomeMod.dict:")
	for k,v in pairs(AwesomeMod.dict) do
		print(tostring(k) .. "=" .. tostring(v))
	end

	print ("-------- AwesomeMod.testSet() --------")
	AwesomeMod.testSet({11, 12, "13", 14, "15", 16, 17, 18, "2019", "2020"}, {5, 4, 3, 2, 1});


	print ("-------- AwesomeMod.testSetSet() --------")
	AwesomeMod.testSetSet({
		{}, 
		{"what", "who", "where"}, 
		{797, 454, 828, "something"}, 
		{"alpha, beta", "gamma"}
	});

	print ("-------- AwesomeMod.testMapMap() --------")
	AwesomeMod.testMapMap({
		animal = {
			dog = "woof",
			cat = "meow",
			cow = "moo"
		},
		rank = {
			first = "Tom",
			second = "Lee",
			third = "Mike"
		}
	});

	if not WITHOUT_CPP_STDLIB then
		print("-------- AwesomeMod.testTuple() --------")
		local values = AwesomeMod.testTuple({"a duck", 999, 1.234})
		print("Lua got multiple values from c++:")
		for i = 1, #values do 
			print("[" .. i .. "]" .. values[i]) 
		end 

		-- AwesomeMod.testTuple2() accept only empty tuple
		local values2 = AwesomeMod.testTuple2({"a duck", 999, 1.234})
		print("Lua got empty tuple from c++.")
		for k,v in pairs(values2) do
			print("[" .. tostring(k) .. "]" .. tostring(v))
		end
	end

	print("-------- AwesomeMod.testMultipleParams() --------")
	AwesomeMod.testMultipleParams(0,1,"two",3.3,44.44)

	print("-------- AwesomeMod.testPosition() --------")
	local positionA = { x = 100, y = 200, z = 300 }
	local positionB = { x = 11, y = 22, z = 33 }
	local result = AwesomeMod.testPosition(positionA, positionB)
	print("positionA["..serialize(positionA).."] + positionB["..serialize(positionB).."] = "..serialize(result))

	if not WITHOUT_CPP_STDLIB then
		print("-------- AwesomeMod.testFunctor --------")
		print(AwesomeMod.testFunctor1(123, 456.78))
		print(AwesomeMod.testFunctor2(789, 111.11))
	end

end


function testCallback ()
	local f = function(a, b, c)
		print ("lua testCallback:")
		print ("    param a:" .. tostring (a))
		print ("    param b:" .. tostring (b))
		print ("    param c:" .. tostring (c))
		print ("    return b * b(" .. tostring(b * b) ..") as result to c++.\n")
		return b * b;
	end

	AwesomeMod.testCallback(f, 5555, "lua text")
end

function testCallbackFunctor ()
	if not WITHOUT_CPP_STDLIB then
		local f = function(a, b, c)
			print ("lua testCallbackFunctor:")
			print ("    param a:" .. tostring (a))
			print ("    param b:" .. tostring (b))
			print ("    param c:" .. tostring (c))
			print ("    return b * b(" .. tostring(b * b) ..") as result to c++.\n")
			return b * b;
		end
		AwesomeMod.testCallbackFunctor(f, 8888, "lua text from testCallbackFunctor")
	end

end

function testAutoGC ()
	local cat = AwesomeCat.new("IWILLLEAVE");
	cat:speak("I will leave ...")
	cat = nil;
	collectgarbage ()
end


function testSingletonAndGC()
	local world = SingletonWorld.new("chaos");
	print("new world tag:"..world:getTag())
	world = nil
	collectgarbage()

	local world = SingletonWorld.getInstance();
	print("singleton world tag:"..world:getTag())
	world = nil
	collectgarbage()


	local world = SingletonWorld.newInstance("new instance");
	print("new world instance tag:"..world:getTag())
	world = nil
	collectgarbage()

	local world=SingletonWorld.managedInstance("managed instance");
	print("managed world instance tag:"..world:getTag())
	world = nil
	collectgarbage()
end


function testClassInheritance()
	SpecialCat = luaaa:extend(AwesomeCat, {value = 1})

	function SpecialCat:onlyInSpecial()
		print(self:getName() .. " has a special cat function")
		print(self:getName() .. " has value:" .. self.value)
	end

	function SpecialCat:speak(text)
		print("Special cat[" .. self:getName() .. "] says: " .. text)
	end

	sss = SpecialCat:new("sss")
	sss:onlyInSpecial()
	sss:speak("I am Special Cat!")
	print("call base class's method speak():")
	luaaa:base(sss):speak("I am Special and Awesome Cat!")

end

print ("\nLUAAA_WITHOUT_CPP_STDLIB:", WITHOUT_CPP_STDLIB);

print ("\n\n-- 1 --. Test auto GC\n")
testAutoGC();

print ("\n\n-- 2 --. Test AwesomeCat class\n")
testAwesomeCat();

print ("\n\n-- 3 --. Test AwesomeMod module\n")
testAwesomeMod ();

print ("\n\n-- 4 --. Test others\n")
print ("pi = " .. pi .. "\n")

print ("\n\n-- 5 --. Test Callback\n")
testCallback();

print ("\n\n-- 6 --. Test CallbackFunctor\n")
testCallbackFunctor();

print ("\n\n-- 7 --. Test Class Inheritance\n")
testClassInheritance();

print ("\n>>>>" .. collectgarbage ("count"))
collectgarbage ()
print ("\n<<<<" .. collectgarbage ("count"))

print("\n\n-- 8 --. Test Singleton and GC\n")
testSingletonAndGC()

print("\n>>>>"..collectgarbage("count"))
collectgarbage()
print("\n<<<<"..collectgarbage("count"))


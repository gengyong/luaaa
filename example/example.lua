

function testAwesomeCat()
	local a = AwesomeCat.new ("BINGO")
	a:setAge(2);
	print(a)
	a:eat({"fish", "milk", "cookie", "rice"});
	print (a)
	a.speak("Thanks!")
end

function testAwesomeMod()
	print ("AwesomeMod.cint:" .. AwesomeMod.cint)
	print ("AwesomeMod.cstr:" .. AwesomeMod.cstr)
	print ("AwesomeMod.dict:")
	for k,v in pairs(AwesomeMod.dict) do
		print(tostring(k) .. "=" .. tostring(v))
	end

	print ("-------- AwesomeMod.testSet() --------")
	AwesomeMod.testSet({1, 2, "4", "5", 6, 7, 8, "1000", "2000"});


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

end


function testAutoGC ()
	local cat = AwesomeCat.new ("IWILLLEAVE");
	cat.speak("I will leave ...")
	cat = nil;
	collectgarbage ()
end

print ("\n\n-- 1 --. Test auto GC\n")
testAutoGC();

print ("\n\n-- 2 --. Test AwesomeCat class\n")
testAwesomeCat();

print ("\n\n-- 3 --. Test AwesomeMod module\n")
testAwesomeMod();

print ("\n\n-- 4 --. Test others\n")
print ("pi = " .. pi .. "\n")

print ("\n>>>>" .. collectgarbage ("count"))
collectgarbage ()
print ("\n<<<<" .. collectgarbage ("count"))
